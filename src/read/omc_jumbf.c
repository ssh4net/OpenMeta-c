#include "omc/omc_jumbf.h"

#include <string.h>

#define OMC_JUMBF_DEF_MAX_INPUT_BYTES ((omc_u64)(64U * 1024U * 1024U))
#define OMC_JUMBF_DEF_MAX_BOX_DEPTH 32U
#define OMC_JUMBF_DEF_MAX_BOXES (1U << 16)
#define OMC_JUMBF_DEF_MAX_ENTRIES 200000U
#define OMC_JUMBF_DEF_MAX_CBOR_DEPTH 64U
#define OMC_JUMBF_DEF_MAX_CBOR_ITEMS 200000U
#define OMC_JUMBF_DEF_MAX_CBOR_KEY_BYTES 1024U
#define OMC_JUMBF_DEF_MAX_CBOR_TEXT_BYTES (8U * 1024U * 1024U)
#define OMC_JUMBF_DEF_MAX_CBOR_BYTES_BYTES (8U * 1024U * 1024U)
#define OMC_ENABLE_C2PA_VERIFY 0
#define OMC_JUMBF_PATH_CAP 1024U
#define OMC_JUMBF_ING_TITLE_CAP 256U
#define OMC_JUMBF_ING_URL_CAP 512U

typedef struct omc_jumbf_box {
    omc_u64 offset;
    omc_u64 size;
    omc_u64 header_size;
    omc_u32 type;
} omc_jumbf_box;

typedef struct omc_jumbf_cbor_head {
    omc_u8 major;
    omc_u8 addl;
    int indefinite;
    omc_u64 arg;
} omc_jumbf_cbor_head;

typedef struct omc_jumbf_ctx {
    omc_store* store;
    omc_block_id block;
    omc_entry_flags flags;
    omc_jumbf_opts opts;
    omc_jumbf_res res;
    omc_u32 order_in_block;
    int c2pa_emitted;
    const omc_u8* input;
    omc_size input_size;
} omc_jumbf_ctx;

typedef struct omc_jumbf_sig_proj {
    char prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 prefix_len;
    omc_u64 key_hits;
    omc_u64 reference_key_hits;
    int linked_to_claim;
    int has_direct_claim_prefix;
    char direct_claim_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 direct_claim_prefix_len;
    int has_claim_ref_index;
    omc_u32 claim_ref_index;
    omc_u32 ref_index_count;
    omc_u32 ref_indices[8];
    omc_u32 ref_label_count;
    char ref_labels[8][128];
    omc_u32 ref_label_lens[8];
    int has_algorithm;
    char algorithm[32];
    omc_u32 algorithm_len;
    int has_unprotected_alg_int;
    omc_s64 unprotected_alg_int;
    int payload_is_null;
    int has_protected_ref;
    omc_byte_ref protected_ref;
    int has_payload_ref;
    omc_byte_ref payload_ref;
    int has_signature_ref;
    omc_byte_ref signature_ref;
    int has_public_key_ref;
    omc_byte_ref public_key_ref;
    int has_certificate_ref;
    omc_byte_ref certificate_ref;
    omc_u32 x5chain_count;
    omc_byte_ref x5chain_refs[8];
} omc_jumbf_sig_proj;

typedef struct omc_jumbf_ing_proj {
    char prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 prefix_len;
    omc_u64 key_hits;
    int has_title;
    char title[OMC_JUMBF_ING_TITLE_CAP];
    omc_u32 title_len;
    int has_relationship;
    char relationship[64];
    omc_u32 relationship_len;
    int has_thumbnail_url;
    char thumbnail_url[OMC_JUMBF_ING_URL_CAP];
    omc_u32 thumbnail_url_len;
} omc_jumbf_ing_proj;

#define OMC_JUMBF_MAX_REL_KINDS 16U

typedef struct omc_jumbf_named_count {
    char name[64];
    omc_u32 name_len;
    omc_u64 count;
} omc_jumbf_named_count;

typedef struct omc_jumbf_ing_summary {
    omc_u64 ingredient_count;
    omc_u64 title_count;
    omc_u64 relationship_count;
    omc_u64 thumbnail_url_count;
    omc_u32 relationship_kind_count;
    omc_u32 named_count_len;
    omc_jumbf_named_count named_counts[OMC_JUMBF_MAX_REL_KINDS];
} omc_jumbf_ing_summary;

typedef struct omc_jumbf_sig_link_proj {
    int has_source_claim_prefix;
    char source_claim_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 source_claim_prefix_len;
    int has_target_claim;
    char target_claim_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 target_claim_prefix_len;
    int has_target_manifest;
    char target_manifest_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 target_manifest_prefix_len;
    int direct_source;
    int cross_source;
    int direct_claim_has_ingredients;
    int explicit_reference_present;
    int explicit_reference_unresolved;
    int explicit_reference_ambiguous;
    omc_u64 explicit_reference_resolved_claim_count;
    omc_u64 explicit_reference_index_hits;
    omc_u64 explicit_reference_label_hits;
    omc_u64 target_direct_claim_count;
    omc_u64 target_cross_claim_count;
    omc_u64 linked_claim_count;
    omc_u64 linked_direct_claim_count;
    omc_u64 linked_cross_claim_count;
    omc_u32 target_claim_count;
    char target_claim_prefixes[8][OMC_JUMBF_PATH_CAP];
    omc_u32 target_claim_lens[8];
    omc_jumbf_ing_summary ingredient_summary;
    omc_jumbf_ing_summary direct_summary;
    omc_jumbf_ing_summary cross_summary;
} omc_jumbf_sig_link_proj;

typedef struct omc_jumbf_claim_link_stats {
    omc_u64 nested_signature_count;
    omc_u64 referenced_by_signature_count;
    omc_u64 linked_signature_count;
    omc_u64 linked_direct_signature_count;
    omc_u64 linked_cross_signature_count;
    omc_jumbf_ing_summary linked_summary;
    omc_jumbf_ing_summary linked_direct_summary;
    omc_jumbf_ing_summary linked_cross_summary;
    omc_u64 linked_explicit_reference_signature_count;
    omc_u64 linked_explicit_reference_direct_signature_count;
    omc_u64 linked_explicit_reference_cross_signature_count;
    omc_jumbf_ing_summary linked_explicit_reference_summary;
    omc_jumbf_ing_summary linked_explicit_reference_direct_summary;
    omc_jumbf_ing_summary linked_explicit_reference_cross_summary;
    omc_u64 linked_explicit_reference_unresolved_signature_count;
    omc_u64 linked_explicit_reference_unresolved_direct_signature_count;
    omc_u64 linked_explicit_reference_unresolved_cross_signature_count;
    omc_jumbf_ing_summary linked_explicit_reference_unresolved_summary;
    omc_jumbf_ing_summary linked_explicit_reference_unresolved_direct_summary;
    omc_jumbf_ing_summary linked_explicit_reference_unresolved_cross_summary;
    omc_u64 linked_explicit_reference_ambiguous_signature_count;
    omc_u64 linked_explicit_reference_ambiguous_direct_signature_count;
    omc_u64 linked_explicit_reference_ambiguous_cross_signature_count;
    omc_jumbf_ing_summary linked_explicit_reference_ambiguous_summary;
    omc_jumbf_ing_summary linked_explicit_reference_ambiguous_direct_summary;
    omc_jumbf_ing_summary linked_explicit_reference_ambiguous_cross_summary;
} omc_jumbf_claim_link_stats;

typedef struct omc_jumbf_sem_proj {
    omc_u64 cbor_key_count;
    omc_u64 active_manifest_count;
    omc_u64 assertion_key_hits;
    omc_u64 assertion_count;
    omc_u64 ingredient_key_hits;
    omc_u64 reference_key_hits;
    omc_u64 ingredient_count;
    omc_u64 ingredient_relationship_count;
    omc_u64 ingredient_thumbnail_url_count;
    omc_jumbf_ing_summary ingredient_summary;
    omc_u64 signature_key_hits;
    omc_u64 manifest_count;
    omc_u64 claim_count;
    omc_u64 signature_count;
    omc_u64 signature_linked_count;
    omc_u64 cross_claim_link_count;
    omc_u64 explicit_reference_signature_count;
    omc_u64 explicit_reference_unresolved_signature_count;
    omc_u64 explicit_reference_ambiguous_signature_count;
    omc_u64 explicit_reference_index_hits;
    omc_u64 explicit_reference_label_hits;
    omc_u64 ingredient_claim_count;
    omc_u64 ingredient_claim_with_signature_count;
    omc_u64 ingredient_claim_referenced_by_signature_count;
    omc_u64 ingredient_manifest_count;
    omc_u64 ingredient_signature_count;
    omc_u64 ingredient_linked_signature_count;
    omc_u64 ingredient_linked_claim_count;
    omc_u64 ingredient_linked_direct_claim_count;
    omc_u64 ingredient_linked_cross_claim_count;
    omc_u64 ingredient_linked_claim_mixed_source_count;
    omc_u64 ingredient_linked_signature_direct_source_count;
    omc_u64 ingredient_linked_signature_cross_source_count;
    omc_u64 ingredient_linked_signature_mixed_source_count;
    omc_u64 ingredient_explicit_reference_signature_count;
    omc_u64 ingredient_explicit_reference_unresolved_signature_count;
    omc_u64 ingredient_explicit_reference_ambiguous_signature_count;
    int has_manifest;
    int has_claim;
    int has_assertion;
    int has_ingredient;
    int has_signature;
    int active_manifest_present;
    int have_claim_generator;
    omc_u32 semantic_sig_index;
} omc_jumbf_sem_proj;

typedef enum omc_c2pa_verify_detail_status {
    OMC_C2PA_VERIFY_DETAIL_NOT_CHECKED = 0,
    OMC_C2PA_VERIFY_DETAIL_PASS = 1,
    OMC_C2PA_VERIFY_DETAIL_FAIL = 2
} omc_c2pa_verify_detail_status;

typedef struct omc_c2pa_profile_summary {
    int available;
    omc_u64 manifest_present;
    omc_u64 claim_present;
    omc_u64 signature_present;
    omc_u64 claim_count;
    omc_u64 signature_count;
    omc_u64 signature_linked;
    omc_u64 signature_orphan;
    omc_u64 explicit_reference_signature_count;
    omc_u64 explicit_reference_unresolved_signature_count;
    omc_u64 explicit_reference_ambiguous_signature_count;
} omc_c2pa_profile_summary;

typedef struct omc_c2pa_verify_evaluation {
    omc_c2pa_verify_status status;
    omc_c2pa_verify_detail_status profile_status;
    const char* profile_reason;
    omc_c2pa_verify_detail_status chain_status;
    const char* chain_reason;
    omc_c2pa_profile_summary profile_summary;
} omc_c2pa_verify_evaluation;

typedef struct omc_c2pa_verify_candidate {
    char prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 prefix_len;
    int has_source_box_prefix;
    char source_box_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 source_box_prefix_len;
    int has_source_claim_prefix;
    char source_claim_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 source_claim_prefix_len;
    int has_algorithm;
    char algorithm[32];
    omc_u32 algorithm_len;
    int has_cose_unprotected_alg_int;
    omc_s64 cose_unprotected_alg_int;
    int has_cose_protected_ref;
    omc_byte_ref cose_protected_ref;
    int has_cose_payload_ref;
    omc_byte_ref cose_payload_ref;
    int cose_payload_is_null;
    int has_cose_signature_ref;
    omc_byte_ref cose_signature_ref;
    int has_signature_ref;
    omc_byte_ref signature_ref;
    int has_public_key_ref;
    omc_byte_ref public_key_ref;
    int has_certificate_ref;
    omc_byte_ref certificate_ref;
    omc_u32 certificate_chain_ref_count;
    omc_byte_ref certificate_chain_refs[8];
    omc_u32 detached_payload_ref_count;
    omc_byte_ref detached_payload_refs[8];
} omc_c2pa_verify_candidate;

typedef struct omc_c2pa_verify_candidate_list {
    omc_u32 count;
    omc_c2pa_verify_candidate items[16];
} omc_c2pa_verify_candidate_list;

static int
omc_jumbf_emit_c2pa_marker(omc_jumbf_ctx* ctx, const char* marker_path,
                           omc_u32 marker_len);

static const char*
omc_c2pa_verify_backend_name(omc_c2pa_verify_backend backend);

static const char*
omc_c2pa_verify_status_name(omc_c2pa_verify_status status);

static const char*
omc_c2pa_verify_detail_status_name(omc_c2pa_verify_detail_status status);

static void
omc_c2pa_profile_summary_init(omc_c2pa_profile_summary* summary);

static void
omc_c2pa_verify_evaluation_init(omc_c2pa_verify_evaluation* evaluation);

static void
omc_c2pa_verify_candidate_list_init(omc_c2pa_verify_candidate_list* list);

static int
omc_c2pa_collect_verify_candidates(
    omc_jumbf_ctx* ctx, omc_size scan_limit,
    omc_c2pa_verify_candidate_list* out_list, int* out_has_signatures);

#if OMC_ENABLE_C2PA_VERIFY
static void
omc_evaluate_c2pa_profile_summary(
    const omc_c2pa_profile_summary* summary,
    int require_resolved_explicit_references,
    omc_c2pa_verify_evaluation* evaluation);
#endif

typedef struct omc_jumbf_exp_ref_scope_stats {
    omc_u64 claim_count;
    omc_u64 direct_claim_count;
    omc_u64 cross_claim_count;
    omc_u64 mixed_source_count;
    omc_u64 direct_source_count;
    omc_u64 cross_source_count;
    omc_jumbf_ing_summary summary;
    omc_jumbf_ing_summary direct_summary;
    omc_jumbf_ing_summary cross_summary;
    omc_u64 unresolved_claim_count;
    omc_u64 unresolved_direct_claim_count;
    omc_u64 unresolved_cross_claim_count;
    omc_u64 unresolved_mixed_source_count;
    omc_u64 unresolved_direct_source_count;
    omc_u64 unresolved_cross_source_count;
    omc_jumbf_ing_summary unresolved_summary;
    omc_jumbf_ing_summary unresolved_direct_summary;
    omc_jumbf_ing_summary unresolved_cross_summary;
    omc_u64 ambiguous_claim_count;
    omc_u64 ambiguous_direct_claim_count;
    omc_u64 ambiguous_cross_claim_count;
    omc_u64 ambiguous_mixed_source_count;
    omc_u64 ambiguous_direct_source_count;
    omc_u64 ambiguous_cross_source_count;
    omc_jumbf_ing_summary ambiguous_summary;
    omc_jumbf_ing_summary ambiguous_direct_summary;
    omc_jumbf_ing_summary ambiguous_cross_summary;
} omc_jumbf_exp_ref_scope_stats;

typedef struct omc_jumbf_claim_signature_stats {
    omc_u64 signature_count;
    omc_u64 signature_linked_count;
    omc_u64 explicit_reference_signature_count;
    omc_u64 explicit_reference_unresolved_signature_count;
    omc_u64 explicit_reference_ambiguous_signature_count;
    omc_u64 explicit_reference_index_hits;
    omc_u64 explicit_reference_label_hits;
} omc_jumbf_claim_signature_stats;

static int
omc_jumbf_prefix_is_nested_under(const char* prefix, omc_u32 prefix_len,
                                 const char* parent, omc_u32 parent_len);

static int
omc_jumbf_collect_signature_projection(omc_jumbf_ctx* ctx, omc_size scan_limit,
                                       const char* prefix, omc_u32 prefix_len,
                                       omc_jumbf_sig_proj* out_sig);

static int
omc_jumbf_build_indexed_root(const char* root, omc_u32 index,
                             const char* field, char* out,
                             omc_u32 out_cap, omc_u32* out_len);

static int
omc_jumbf_emit_indexed_field_text(omc_jumbf_ctx* ctx, const char* root,
                                  omc_u32 index, const char* field,
                                  const char* text, omc_u32 text_len);

static int
omc_jumbf_emit_indexed_field_u64(omc_jumbf_ctx* ctx, const char* root,
                                 omc_u32 index, const char* field,
                                 omc_u64 value);

static omc_u32
omc_jumbf_cstr_size(const char* text)
{
    omc_u32 len;

    len = 0U;
    if (text == (const char*)0) {
        return 0U;
    }
    while (text[len] != '\0') {
        len += 1U;
    }
    return len;
}

static int
omc_jumbf_cstr_eq(const char* lhs, const char* rhs)
{
    omc_u32 i;

    if (lhs == (const char*)0 || rhs == (const char*)0) {
        return 0;
    }
    i = 0U;
    while (lhs[i] != '\0' && rhs[i] != '\0') {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
        i += 1U;
    }
    return lhs[i] == rhs[i];
}

static int
omc_jumbf_is_digit(omc_u8 ch)
{
    return ch >= (omc_u8)'0' && ch <= (omc_u8)'9';
}

static int
omc_jumbf_is_ascii_alnum(omc_u8 ch)
{
    if (ch >= (omc_u8)'0' && ch <= (omc_u8)'9') {
        return 1;
    }
    if (ch >= (omc_u8)'A' && ch <= (omc_u8)'Z') {
        return 1;
    }
    if (ch >= (omc_u8)'a' && ch <= (omc_u8)'z') {
        return 1;
    }
    return 0;
}

static int
omc_jumbf_is_printable_ascii(omc_u8 ch)
{
    return ch >= 0x20U && ch <= 0x7EU;
}

static int
omc_jumbf_bytes_all_ascii_printable(const omc_u8* bytes, omc_u32 size)
{
    omc_u32 i;

    if (bytes == (const omc_u8*)0) {
        return 0;
    }
    for (i = 0U; i < size; ++i) {
        if (!omc_jumbf_is_printable_ascii(bytes[i])) {
            return 0;
        }
    }
    return 1;
}

static int
omc_jumbf_to_lower_eq(omc_u8 byte, char want)
{
    if (byte >= (omc_u8)'A' && byte <= (omc_u8)'Z') {
        byte = (omc_u8)(byte + ((omc_u8)'a' - (omc_u8)'A'));
    }
    return byte == (omc_u8)want;
}

static int
omc_jumbf_append_char(char* out, omc_u32 cap, omc_u32* io_len, char ch)
{
    if (out == (char*)0 || io_len == (omc_u32*)0 || cap == 0U) {
        return 0;
    }
    if (*io_len + 1U >= cap) {
        return 0;
    }
    out[*io_len] = ch;
    *io_len += 1U;
    out[*io_len] = '\0';
    return 1;
}

static int
omc_jumbf_path_separator(omc_u8 ch)
{
    return ch == (omc_u8)'.' || ch == (omc_u8)'[' || ch == (omc_u8)']'
           || ch == (omc_u8)'@';
}

static int
omc_jumbf_view_starts_with(omc_const_bytes view, const char* prefix)
{
    omc_u32 prefix_len;

    prefix_len = omc_jumbf_cstr_size(prefix);
    if (view.data == (const omc_u8*)0 || prefix == (const char*)0
        || view.size < prefix_len) {
        return 0;
    }
    return memcmp(view.data, prefix, prefix_len) == 0;
}

static int
omc_jumbf_view_eq_cstr(omc_const_bytes view, const char* text)
{
    omc_u32 len;

    len = omc_jumbf_cstr_size(text);
    if (view.data == (const omc_u8*)0 || text == (const char*)0) {
        return 0;
    }
    return view.size == len && memcmp(view.data, text, len) == 0;
}

static int
omc_jumbf_view_has_segment(omc_const_bytes view, const char* segment)
{
    omc_u32 segment_len;
    omc_u32 pos;

    if (view.data == (const omc_u8*)0 || segment == (const char*)0) {
        return 0;
    }
    segment_len = omc_jumbf_cstr_size(segment);
    if (segment_len == 0U || view.size < segment_len) {
        return 0;
    }

    for (pos = 0U; pos + segment_len <= view.size; ++pos) {
        omc_u32 end;
        int left_ok;
        int right_ok;

        if (memcmp(view.data + pos, segment, segment_len) != 0) {
            continue;
        }
        end = pos + segment_len;
        left_ok = (pos == 0U) || omc_jumbf_path_separator(view.data[pos - 1U]);
        right_ok = (end >= view.size)
                   || omc_jumbf_path_separator(view.data[end]);
        if (left_ok && right_ok) {
            return 1;
        }
    }
    return 0;
}

static int
omc_jumbf_view_find(omc_const_bytes view, const char* needle, omc_u32* out_pos)
{
    omc_u32 needle_len;
    omc_u32 pos;

    if (out_pos == (omc_u32*)0 || view.data == (const omc_u8*)0
        || needle == (const char*)0) {
        return 0;
    }
    needle_len = omc_jumbf_cstr_size(needle);
    if (needle_len == 0U || view.size < needle_len) {
        return 0;
    }
    for (pos = 0U; pos + needle_len <= view.size; ++pos) {
        if (memcmp(view.data + pos, needle, needle_len) == 0) {
            *out_pos = pos;
            return 1;
        }
    }
    return 0;
}

static omc_const_bytes
omc_jumbf_const_bytes(const void* data, omc_size size)
{
    omc_const_bytes view;

    view.data = (const omc_u8*)data;
    view.size = size;
    return view;
}

static int
omc_jumbf_append_mem(char* out, omc_u32 cap, omc_u32* io_len,
                     const char* src, omc_u32 src_len)
{
    if (out == (char*)0 || io_len == (omc_u32*)0) {
        return 0;
    }
    if (src_len > 0U && src == (const char*)0) {
        return 0;
    }
    if (*io_len + src_len >= cap) {
        return 0;
    }
    if (src_len != 0U) {
        memcpy(out + *io_len, src, src_len);
        *io_len += src_len;
    }
    out[*io_len] = '\0';
    return 1;
}

static int
omc_jumbf_append_u32_dec(char* out, omc_u32 cap, omc_u32* io_len,
                         omc_u32 value)
{
    char tmp[16];
    omc_u32 n;
    omc_u32 i;

    n = 0U;
    do {
        tmp[n] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        n += 1U;
    } while (value != 0U && n < (omc_u32)sizeof(tmp));

    if (*io_len + n >= cap) {
        return 0;
    }
    for (i = 0U; i < n; ++i) {
        out[*io_len + i] = tmp[n - 1U - i];
    }
    *io_len += n;
    out[*io_len] = '\0';
    return 1;
}

static int
omc_jumbf_append_u64_dec(char* out, omc_u32 cap, omc_u32* io_len,
                         omc_u64 value)
{
    char tmp[32];
    omc_u32 n;
    omc_u32 i;

    n = 0U;
    do {
        tmp[n] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        n += 1U;
    } while (value != 0U && n < (omc_u32)sizeof(tmp));

    if (*io_len + n >= cap) {
        return 0;
    }
    for (i = 0U; i < n; ++i) {
        out[*io_len + i] = tmp[n - 1U - i];
    }
    *io_len += n;
    out[*io_len] = '\0';
    return 1;
}

static int
omc_jumbf_make_child_path(const char* parent, omc_u32 parent_len,
                          omc_u32 child_index, char* out, omc_u32 cap,
                          omc_u32* out_len)
{
    omc_u32 len;

    len = 0U;
    if (out == (char*)0 || out_len == (omc_u32*)0 || cap == 0U) {
        return 0;
    }
    out[0] = '\0';

    if (parent_len == 0U) {
        if (!omc_jumbf_append_mem(out, cap, &len, "box.", 4U)) {
            return 0;
        }
    } else {
        if (!omc_jumbf_append_mem(out, cap, &len, parent, parent_len)
            || !omc_jumbf_append_char(out, cap, &len, '.')) {
            return 0;
        }
    }
    if (!omc_jumbf_append_u32_dec(out, cap, &len, child_index)) {
        return 0;
    }
    *out_len = len;
    return 1;
}

static int
omc_jumbf_make_field_key(const char* path, omc_u32 path_len,
                         const char* suffix, char* out, omc_u32 cap,
                         omc_u32* out_len)
{
    omc_u32 len;
    omc_u32 suffix_len;

    len = 0U;
    suffix_len = omc_jumbf_cstr_size(suffix);

    if (out == (char*)0 || out_len == (omc_u32*)0 || cap == 0U) {
        return 0;
    }
    out[0] = '\0';

    if (!omc_jumbf_append_mem(out, cap, &len, path, path_len)
        || !omc_jumbf_append_char(out, cap, &len, '.')
        || !omc_jumbf_append_mem(out, cap, &len, suffix, suffix_len)) {
        return 0;
    }

    *out_len = len;
    return 1;
}

static int
omc_jumbf_make_array_path(const char* prefix, omc_u32 prefix_len,
                          omc_u32 index, char* out, omc_u32 cap,
                          omc_u32* out_len)
{
    omc_u32 len;

    len = 0U;
    if (out == (char*)0 || out_len == (omc_u32*)0 || cap == 0U) {
        return 0;
    }
    out[0] = '\0';

    if (!omc_jumbf_append_mem(out, cap, &len, prefix, prefix_len)
        || !omc_jumbf_append_char(out, cap, &len, '[')
        || !omc_jumbf_append_u32_dec(out, cap, &len, index)
        || !omc_jumbf_append_char(out, cap, &len, ']')) {
        return 0;
    }

    *out_len = len;
    return 1;
}

static int
omc_jumbf_make_path_with_segment(const char* prefix, omc_u32 prefix_len,
                                 const char* segment, omc_u32 segment_len,
                                 char* out, omc_u32 cap, omc_u32* out_len)
{
    omc_u32 len;

    len = 0U;
    if (out == (char*)0 || out_len == (omc_u32*)0 || cap == 0U) {
        return 0;
    }
    out[0] = '\0';

    if (!omc_jumbf_append_mem(out, cap, &len, prefix, prefix_len)) {
        return 0;
    }
    if (segment_len != 0U) {
        if (!omc_jumbf_append_char(out, cap, &len, '.')
            || !omc_jumbf_append_mem(out, cap, &len, segment, segment_len)) {
            return 0;
        }
    }

    *out_len = len;
    return 1;
}

static int
omc_jumbf_read_u32be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                     omc_u32* out_value)
{
    if (out_value == (omc_u32*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 4U) {
        return 0;
    }

    *out_value = (((omc_u32)bytes[(omc_size)offset + 0U]) << 24)
                 | (((omc_u32)bytes[(omc_size)offset + 1U]) << 16)
                 | (((omc_u32)bytes[(omc_size)offset + 2U]) << 8)
                 | (((omc_u32)bytes[(omc_size)offset + 3U]) << 0);
    return 1;
}

static int
omc_jumbf_read_u64be(const omc_u8* bytes, omc_size size, omc_u64 offset,
                     omc_u64* out_value)
{
    if (out_value == (omc_u64*)0) {
        return 0;
    }
    if (offset > (omc_u64)size || ((omc_u64)size - offset) < 8U) {
        return 0;
    }

    *out_value = (((omc_u64)bytes[(omc_size)offset + 0U]) << 56)
                 | (((omc_u64)bytes[(omc_size)offset + 1U]) << 48)
                 | (((omc_u64)bytes[(omc_size)offset + 2U]) << 40)
                 | (((omc_u64)bytes[(omc_size)offset + 3U]) << 32)
                 | (((omc_u64)bytes[(omc_size)offset + 4U]) << 24)
                 | (((omc_u64)bytes[(omc_size)offset + 5U]) << 16)
                 | (((omc_u64)bytes[(omc_size)offset + 6U]) << 8)
                 | (((omc_u64)bytes[(omc_size)offset + 7U]) << 0);
    return 1;
}

static int
omc_jumbf_parse_box(const omc_u8* bytes, omc_size size, omc_u64 begin,
                    omc_u64 end, omc_jumbf_box* out_box)
{
    omc_u32 size32;
    omc_u64 box_size;
    omc_u64 header_size;

    if (out_box == (omc_jumbf_box*)0) {
        return 0;
    }
    if (bytes == (const omc_u8*)0) {
        return 0;
    }
    if (begin > end || end > (omc_u64)size) {
        return 0;
    }
    if (end - begin < 8U) {
        return 0;
    }
    if (!omc_jumbf_read_u32be(bytes, size, begin, &size32)
        || !omc_jumbf_read_u32be(bytes, size, begin + 4U, &out_box->type)) {
        return 0;
    }

    header_size = 8U;
    box_size = (omc_u64)size32;
    if (size32 == 1U) {
        if (!omc_jumbf_read_u64be(bytes, size, begin + 8U, &box_size)) {
            return 0;
        }
        header_size = 16U;
    } else if (size32 == 0U) {
        box_size = end - begin;
    }

    if (out_box->type == OMC_FOURCC('u', 'u', 'i', 'd')) {
        header_size += 16U;
    }
    if (box_size < header_size || box_size > end - begin) {
        return 0;
    }

    out_box->offset = begin;
    out_box->size = box_size;
    out_box->header_size = header_size;
    return 1;
}

static int
omc_jumbf_looks_like_bmff_sequence(const omc_u8* bytes, omc_size size,
                                   omc_u64 begin, omc_u64 end)
{
    omc_jumbf_box box;

    if (bytes == (const omc_u8*)0 || begin > end || end > (omc_u64)size) {
        return 0;
    }
    if (end - begin < 8U) {
        return 0;
    }
    if (!omc_jumbf_parse_box(bytes, size, begin, end, &box)) {
        return 0;
    }
    if (box.size == 0U || box.header_size > box.size) {
        return 0;
    }
    return 1;
}

static void
omc_jumbf_opts_norm(omc_jumbf_opts* dst, const omc_jumbf_opts* src)
{
    if (dst == (omc_jumbf_opts*)0) {
        return;
    }
    if (src == (const omc_jumbf_opts*)0) {
        omc_jumbf_opts_init(dst);
        return;
    }

    *dst = *src;
    if (dst->limits.max_box_depth == 0U) {
        dst->limits.max_box_depth = OMC_JUMBF_DEF_MAX_BOX_DEPTH;
    }
    if (dst->limits.max_boxes == 0U) {
        dst->limits.max_boxes = OMC_JUMBF_DEF_MAX_BOXES;
    }
    if (dst->limits.max_entries == 0U) {
        dst->limits.max_entries = OMC_JUMBF_DEF_MAX_ENTRIES;
    }
    if (dst->limits.max_cbor_depth == 0U) {
        dst->limits.max_cbor_depth = OMC_JUMBF_DEF_MAX_CBOR_DEPTH;
    }
    if (dst->limits.max_cbor_items == 0U) {
        dst->limits.max_cbor_items = OMC_JUMBF_DEF_MAX_CBOR_ITEMS;
    }
    if (dst->limits.max_cbor_key_bytes == 0U) {
        dst->limits.max_cbor_key_bytes = OMC_JUMBF_DEF_MAX_CBOR_KEY_BYTES;
    }
    if (dst->limits.max_cbor_text_bytes == 0U) {
        dst->limits.max_cbor_text_bytes = OMC_JUMBF_DEF_MAX_CBOR_TEXT_BYTES;
    }
    if (dst->limits.max_cbor_bytes_bytes == 0U) {
        dst->limits.max_cbor_bytes_bytes = OMC_JUMBF_DEF_MAX_CBOR_BYTES_BYTES;
    }
}

static int
omc_jumbf_status_from_store(omc_status st, omc_jumbf_status* out_status)
{
    if (out_status == (omc_jumbf_status*)0) {
        return 0;
    }
    if (st == OMC_STATUS_OK) {
        *out_status = OMC_JUMBF_OK;
        return 1;
    }
    if (st == OMC_STATUS_NO_MEMORY) {
        *out_status = OMC_JUMBF_NOMEM;
    } else {
        *out_status = OMC_JUMBF_LIMIT;
    }
    return 0;
}

static int
omc_jumbf_store_bytes(omc_jumbf_ctx* ctx, const void* src, omc_size size,
                      omc_byte_ref* out_ref)
{
    omc_status st;

    if (ctx == (omc_jumbf_ctx*)0 || out_ref == (omc_byte_ref*)0) {
        return 0;
    }
    if (ctx->store == (omc_store*)0) {
        out_ref->offset = 0U;
        out_ref->size = (omc_u32)size;
        return 1;
    }

    st = omc_arena_append(&ctx->store->arena, src, size, out_ref);
    return omc_jumbf_status_from_store(st, &ctx->res.status);
}

static int
omc_jumbf_take_entry(omc_jumbf_ctx* ctx)
{
    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->opts.limits.max_entries != 0U
        && ctx->res.entries_decoded >= ctx->opts.limits.max_entries) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return 1;
}

static int
omc_jumbf_take_box(omc_jumbf_ctx* ctx)
{
    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    ctx->res.boxes_decoded += 1U;
    if (ctx->opts.limits.max_boxes != 0U
        && ctx->res.boxes_decoded > ctx->opts.limits.max_boxes) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return 1;
}

static int
omc_jumbf_take_cbor_item(omc_jumbf_ctx* ctx)
{
    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    ctx->res.cbor_items += 1U;
    if (ctx->opts.limits.max_cbor_items != 0U
        && ctx->res.cbor_items > ctx->opts.limits.max_cbor_items) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return 1;
}

static int
omc_jumbf_add_entry(omc_jumbf_ctx* ctx, const omc_key* key,
                    const omc_val* value, omc_entry_flags extra_flags)
{
    omc_entry entry;
    omc_status st;

    if (ctx == (omc_jumbf_ctx*)0 || key == (const omc_key*)0
        || value == (const omc_val*)0) {
        return 0;
    }
    if (!omc_jumbf_take_entry(ctx)) {
        return 0;
    }
    if (ctx->store == (omc_store*)0) {
        ctx->res.entries_decoded += 1U;
        ctx->order_in_block += 1U;
        return 1;
    }

    memset(&entry, 0, sizeof(entry));
    entry.key = *key;
    entry.value = *value;
    entry.origin.block = ctx->block;
    entry.origin.order_in_block = ctx->order_in_block;
    entry.origin.wire_type.family = OMC_WIRE_OTHER;
    entry.origin.wire_type.code = 0U;
    entry.origin.wire_count = 1U;
    entry.flags = ctx->flags | extra_flags;

    st = omc_store_add_entry(ctx->store, &entry, (omc_entry_id*)0);
    if (st != OMC_STATUS_OK) {
        (void)omc_jumbf_status_from_store(st, &ctx->res.status);
        return 0;
    }

    ctx->res.entries_decoded += 1U;
    ctx->order_in_block += 1U;
    return 1;
}

static int
omc_jumbf_emit_field_ref(omc_jumbf_ctx* ctx, const char* field_key,
                         omc_u32 field_len, omc_byte_ref value_ref,
                         int is_text, omc_text_encoding enc)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, field_key, field_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_field(&key, key_ref);
        if (is_text) {
            omc_val_make_text(&val, value_ref, enc);
        } else {
            omc_val_make_bytes(&val, value_ref);
        }
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_DERIVED);
}

static int
omc_jumbf_emit_field_text(omc_jumbf_ctx* ctx, const char* field_key,
                          omc_u32 field_len, const char* value,
                          omc_u32 value_len)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;
    omc_byte_ref value_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, field_key, field_len, &key_ref)
            || !omc_jumbf_store_bytes(ctx, value, value_len, &value_ref)) {
            return 0;
        }
        omc_key_make_jumbf_field(&key, key_ref);
        omc_val_make_text(&val, value_ref, OMC_TEXT_ASCII);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_DERIVED);
}

static int
omc_jumbf_emit_field_bytes_ref(omc_jumbf_ctx* ctx, const char* field_key,
                               omc_u32 field_len, omc_byte_ref value_ref)
{
    return omc_jumbf_emit_field_ref(ctx, field_key, field_len, value_ref, 0,
                                    OMC_TEXT_UNKNOWN);
}

static int
omc_jumbf_emit_field_u64(omc_jumbf_ctx* ctx, const char* field_key,
                         omc_u32 field_len, omc_u64 value)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, field_key, field_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_field(&key, key_ref);
        omc_val_make_u64(&val, value);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_DERIVED);
}

static int
omc_jumbf_emit_field_i64(omc_jumbf_ctx* ctx, const char* field_key,
                         omc_u32 field_len, omc_s64 value)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, field_key, field_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_field(&key, key_ref);
        omc_val_make_i64(&val, value);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_DERIVED);
}

static int
omc_jumbf_emit_field_u32(omc_jumbf_ctx* ctx, const char* field_key,
                         omc_u32 field_len, omc_u32 value)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, field_key, field_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_field(&key, key_ref);
        omc_val_make_u32(&val, value);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_DERIVED);
}

static int
omc_jumbf_emit_field_u8(omc_jumbf_ctx* ctx, const char* field_key,
                        omc_u32 field_len, omc_u8 value)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, field_key, field_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_field(&key, key_ref);
        omc_val_make_u8(&val, value);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_DERIVED);
}

static int
omc_jumbf_emit_c2pa_verify_scaffold(omc_jumbf_ctx* ctx, int has_signatures,
                                    int c2pa_detected,
                                    const omc_c2pa_profile_summary* summary)
{
    const int requested = ctx != (omc_jumbf_ctx*)0 && ctx->opts.verify_c2pa;
    omc_c2pa_verify_backend selected;
    omc_c2pa_verify_status verify_status;
    omc_c2pa_verify_evaluation evaluation;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }

    if (ctx->store != (omc_store*)0) {
        omc_c2pa_verify_candidate_list candidates;
        int collected_has_signatures;

        collected_has_signatures = 0;
        if (omc_c2pa_collect_verify_candidates(
                ctx, ctx->store->entry_count, &candidates,
                &collected_has_signatures)) {
            if (collected_has_signatures || candidates.count != 0U) {
                has_signatures = 1;
            }
        } else if (ctx->res.status != OMC_JUMBF_OK) {
            return 0;
        }
    }

    omc_c2pa_verify_evaluation_init(&evaluation);
    selected = OMC_C2PA_VERIFY_BACKEND_NONE;
    verify_status = OMC_C2PA_VERIFY_NOT_REQUESTED;
    if (requested) {
#if OMC_ENABLE_C2PA_VERIFY
        selected = ctx->opts.verify_backend;
        if (has_signatures) {
            omc_evaluate_c2pa_profile_summary(
                summary, ctx->opts.verify_require_resolved_references,
                &evaluation);
            if (evaluation.profile_status == OMC_C2PA_VERIFY_DETAIL_FAIL) {
                verify_status = OMC_C2PA_VERIFY_VERIFICATION_FAILED;
            } else {
                verify_status = OMC_C2PA_VERIFY_NOT_IMPLEMENTED;
            }
        } else {
            verify_status = OMC_C2PA_VERIFY_NO_SIGNATURES;
        }
#else
        (void)summary;
        verify_status = OMC_C2PA_VERIFY_DISABLED_BY_BUILD;
#endif
    }
    ctx->res.verify_status = verify_status;
    ctx->res.verify_backend_selected = selected;

    if (!requested && !c2pa_detected && !has_signatures) {
        return 1;
    }
    if ((c2pa_detected || has_signatures)
        && !omc_jumbf_emit_c2pa_marker(
               ctx, "verify.scaffold",
               omc_jumbf_cstr_size("verify.scaffold"))) {
        return 0;
    }
    if (!omc_jumbf_emit_field_u8(
               ctx, "c2pa.verify.requested",
               omc_jumbf_cstr_size("c2pa.verify.requested"),
               (omc_u8)(requested ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.verify.require_resolved_references",
               omc_jumbf_cstr_size(
                   "c2pa.verify.require_resolved_references"),
               (omc_u8)(ctx->opts.verify_require_resolved_references ? 1U
                                                                     : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.verify.enabled_in_build",
               omc_jumbf_cstr_size("c2pa.verify.enabled_in_build"),
#if OMC_ENABLE_C2PA_VERIFY
               1U)
#else
               0U)
#endif
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.verify.signatures_present",
               omc_jumbf_cstr_size("c2pa.verify.signatures_present"),
               (omc_u8)(has_signatures ? 1U : 0U))
        || !omc_jumbf_emit_field_text(
               ctx, "c2pa.verify.backend_requested",
               omc_jumbf_cstr_size("c2pa.verify.backend_requested"),
               omc_c2pa_verify_backend_name(ctx->opts.verify_backend),
               (omc_u32)strlen(
                   omc_c2pa_verify_backend_name(ctx->opts.verify_backend)))
        || !omc_jumbf_emit_field_text(
               ctx, "c2pa.verify.backend_selected",
               omc_jumbf_cstr_size("c2pa.verify.backend_selected"),
               omc_c2pa_verify_backend_name(selected),
               (omc_u32)strlen(omc_c2pa_verify_backend_name(selected)))
        || !omc_jumbf_emit_field_text(
               ctx, "c2pa.verify.status",
               omc_jumbf_cstr_size("c2pa.verify.status"),
               omc_c2pa_verify_status_name(verify_status),
               (omc_u32)strlen(omc_c2pa_verify_status_name(verify_status)))
        || !omc_jumbf_emit_field_text(
               ctx, "c2pa.verify.profile_status",
               omc_jumbf_cstr_size("c2pa.verify.profile_status"),
               omc_c2pa_verify_detail_status_name(
                   evaluation.profile_status),
               (omc_u32)strlen(omc_c2pa_verify_detail_status_name(
                   evaluation.profile_status)))
        || !omc_jumbf_emit_field_text(
               ctx, "c2pa.verify.profile_reason",
               omc_jumbf_cstr_size("c2pa.verify.profile_reason"),
               evaluation.profile_reason,
               (omc_u32)strlen(evaluation.profile_reason))
        || !omc_jumbf_emit_field_text(
               ctx, "c2pa.verify.chain_status",
               omc_jumbf_cstr_size("c2pa.verify.chain_status"),
               omc_c2pa_verify_detail_status_name(
                   evaluation.chain_status),
               (omc_u32)strlen(omc_c2pa_verify_detail_status_name(
                   evaluation.chain_status)))
        || !omc_jumbf_emit_field_text(
               ctx, "c2pa.verify.chain_reason",
               omc_jumbf_cstr_size("c2pa.verify.chain_reason"),
               evaluation.chain_reason,
               (omc_u32)strlen(evaluation.chain_reason))
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.verify.claim_count",
               omc_jumbf_cstr_size("c2pa.verify.claim_count"),
               evaluation.profile_summary.claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.verify.signature_count",
               omc_jumbf_cstr_size("c2pa.verify.signature_count"),
               evaluation.profile_summary.signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.verify.signature_linked_count",
               omc_jumbf_cstr_size("c2pa.verify.signature_linked_count"),
               evaluation.profile_summary.signature_linked)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.verify.signature_orphan_count",
               omc_jumbf_cstr_size("c2pa.verify.signature_orphan_count"),
               evaluation.profile_summary.signature_orphan)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.verify.explicit_reference_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.verify.explicit_reference_signature_count"),
               evaluation.profile_summary.explicit_reference_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.verify.explicit_reference_unresolved_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.verify.explicit_reference_unresolved_signature_count"),
               evaluation.profile_summary
                   .explicit_reference_unresolved_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.verify.explicit_reference_ambiguous_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.verify.explicit_reference_ambiguous_signature_count"),
               evaluation.profile_summary
                   .explicit_reference_ambiguous_signature_count)) {
        return 0;
    }
    return 1;
}

static int
omc_jumbf_emit_cbor_u64(omc_jumbf_ctx* ctx, const char* key_text,
                        omc_u32 key_len, omc_u64 value)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        omc_val_make_u64(&val, value);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_emit_cbor_i64(omc_jumbf_ctx* ctx, const char* key_text,
                        omc_u32 key_len, omc_s64 value)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        omc_val_make_i64(&val, value);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_emit_cbor_u8(omc_jumbf_ctx* ctx, const char* key_text,
                       omc_u32 key_len, omc_u8 value)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        omc_val_make_u8(&val, value);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_emit_cbor_f32_bits(omc_jumbf_ctx* ctx, const char* key_text,
                             omc_u32 key_len, omc_u32 bits)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        omc_val_make_f32_bits(&val, bits);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_emit_cbor_f64_bits(omc_jumbf_ctx* ctx, const char* key_text,
                             omc_u32 key_len, omc_u64 bits)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        omc_val_make_f64_bits(&val, bits);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_emit_cbor_text(omc_jumbf_ctx* ctx, const char* key_text,
                         omc_u32 key_len, const omc_u8* value,
                         omc_u32 value_len, omc_text_encoding enc)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;
    omc_byte_ref value_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)
            || !omc_jumbf_store_bytes(ctx, value, value_len, &value_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        omc_val_make_text(&val, value_ref, enc);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_emit_cbor_bytes(omc_jumbf_ctx* ctx, const char* key_text,
                          omc_u32 key_len, const omc_u8* value,
                          omc_u32 value_len)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;
    omc_byte_ref value_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)
            || !omc_jumbf_store_bytes(ctx, value, value_len, &value_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        omc_val_make_bytes(&val, value_ref);
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_emit_cbor_ref(omc_jumbf_ctx* ctx, const char* key_text,
                        omc_u32 key_len, omc_byte_ref value_ref, int is_text,
                        omc_text_encoding enc)
{
    omc_key key;
    omc_val val;
    omc_byte_ref key_ref;

    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->store != (omc_store*)0) {
        if (!omc_jumbf_store_bytes(ctx, key_text, key_len, &key_ref)) {
            return 0;
        }
        omc_key_make_jumbf_cbor_key(&key, key_ref);
        if (is_text) {
            omc_val_make_text(&val, value_ref, enc);
        } else {
            omc_val_make_bytes(&val, value_ref);
        }
    } else {
        omc_key_init(&key);
        omc_val_init(&val);
    }
    return omc_jumbf_add_entry(ctx, &key, &val, OMC_ENTRY_FLAG_NONE);
}

static int
omc_jumbf_ascii_icase_contains(const omc_u8* bytes, omc_u64 size,
                               const char* needle, omc_u32 max_scan)
{
    omc_u32 needle_len;
    omc_u64 limit;
    omc_u64 i;

    if (bytes == (const omc_u8*)0 || needle == (const char*)0) {
        return 0;
    }

    needle_len = omc_jumbf_cstr_size(needle);
    if (needle_len == 0U) {
        return 0;
    }

    limit = size;
    if (limit > (omc_u64)max_scan) {
        limit = (omc_u64)max_scan;
    }
    if (limit < (omc_u64)needle_len) {
        return 0;
    }

    for (i = 0U; i + (omc_u64)needle_len <= limit; ++i) {
        omc_u32 j;
        int match;

        match = 1;
        for (j = 0U; j < needle_len; ++j) {
            if (!omc_jumbf_to_lower_eq(bytes[(omc_size)(i + j)],
                                       needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) {
            return 1;
        }
    }
    return 0;
}

static int
omc_jumbf_emit_c2pa_marker(omc_jumbf_ctx* ctx, const char* marker_path,
                           omc_u32 marker_len)
{
    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->c2pa_emitted) {
        return 1;
    }
    if (!omc_jumbf_emit_field_u32(ctx, "c2pa.detected", 13U, 1U)) {
        return 0;
    }
    if (marker_path != (const char*)0 && marker_len != 0U) {
        if (!omc_jumbf_emit_field_text(ctx, "c2pa.marker_path", 16U,
                                       marker_path, marker_len)) {
            return 0;
        }
    }
    ctx->c2pa_emitted = 1;
    return 1;
}

static const char*
omc_c2pa_verify_backend_name(omc_c2pa_verify_backend backend)
{
    switch (backend) {
        case OMC_C2PA_VERIFY_BACKEND_NONE: return "none";
        case OMC_C2PA_VERIFY_BACKEND_AUTO: return "auto";
        case OMC_C2PA_VERIFY_BACKEND_NATIVE: return "native";
        case OMC_C2PA_VERIFY_BACKEND_OPENSSL: return "openssl";
    }
    return "unknown";
}

static const char*
omc_c2pa_verify_status_name(omc_c2pa_verify_status status)
{
    switch (status) {
        case OMC_C2PA_VERIFY_NOT_REQUESTED: return "not_requested";
        case OMC_C2PA_VERIFY_DISABLED_BY_BUILD: return "disabled_by_build";
        case OMC_C2PA_VERIFY_BACKEND_UNAVAILABLE: return "backend_unavailable";
        case OMC_C2PA_VERIFY_NO_SIGNATURES: return "no_signatures";
        case OMC_C2PA_VERIFY_INVALID_SIGNATURE: return "invalid_signature";
        case OMC_C2PA_VERIFY_VERIFICATION_FAILED:
            return "verification_failed";
        case OMC_C2PA_VERIFY_VERIFIED: return "verified";
        case OMC_C2PA_VERIFY_NOT_IMPLEMENTED: return "not_implemented";
    }
    return "unknown";
}

static const char*
omc_c2pa_verify_detail_status_name(omc_c2pa_verify_detail_status status)
{
    switch (status) {
        case OMC_C2PA_VERIFY_DETAIL_NOT_CHECKED: return "not_checked";
        case OMC_C2PA_VERIFY_DETAIL_PASS: return "pass";
        case OMC_C2PA_VERIFY_DETAIL_FAIL: return "fail";
    }
    return "unknown";
}

static void
omc_c2pa_profile_summary_init(omc_c2pa_profile_summary* summary)
{
    if (summary == (omc_c2pa_profile_summary*)0) {
        return;
    }
    memset(summary, 0, sizeof(*summary));
}

static void
omc_c2pa_verify_evaluation_init(omc_c2pa_verify_evaluation* evaluation)
{
    if (evaluation == (omc_c2pa_verify_evaluation*)0) {
        return;
    }
    evaluation->status = OMC_C2PA_VERIFY_NOT_REQUESTED;
    evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_NOT_CHECKED;
    evaluation->profile_reason = "not_checked";
    evaluation->chain_status = OMC_C2PA_VERIFY_DETAIL_NOT_CHECKED;
    evaluation->chain_reason = "not_checked";
    omc_c2pa_profile_summary_init(&evaluation->profile_summary);
}

static void
omc_c2pa_verify_candidate_list_init(omc_c2pa_verify_candidate_list* list)
{
    if (list == (omc_c2pa_verify_candidate_list*)0) {
        return;
    }
    memset(list, 0, sizeof(*list));
}

#if OMC_ENABLE_C2PA_VERIFY
static void
omc_evaluate_c2pa_profile_summary(
    const omc_c2pa_profile_summary* summary,
    int require_resolved_explicit_references,
    omc_c2pa_verify_evaluation* evaluation)
{
    if (evaluation == (omc_c2pa_verify_evaluation*)0) {
        return;
    }

    if (summary == (const omc_c2pa_profile_summary*)0) {
        omc_c2pa_profile_summary_init(&evaluation->profile_summary);
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_NOT_CHECKED;
        evaluation->profile_reason = "semantic_fields_missing";
        return;
    }

    evaluation->profile_summary = *summary;
    if (!summary->available) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_NOT_CHECKED;
        evaluation->profile_reason = "semantic_fields_missing";
        return;
    }

    if (summary->manifest_present == 0U) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "manifest_missing";
        return;
    }
    if (summary->claim_count == 0U || summary->claim_present == 0U) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "claim_missing";
        return;
    }
    if (summary->signature_count == 0U || summary->signature_present == 0U) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "signature_missing";
        return;
    }
    if (summary->signature_linked > summary->signature_count) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "signature_linked_count_invalid";
        return;
    }
    if (summary->signature_orphan > summary->signature_count) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "signature_orphan_count_invalid";
        return;
    }
    if ((summary->signature_linked + summary->signature_orphan)
        != summary->signature_count) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "signature_link_consistency_invalid";
        return;
    }
    if (summary->explicit_reference_signature_count > summary->signature_count) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "explicit_reference_count_invalid";
        return;
    }
    if (summary->explicit_reference_unresolved_signature_count
        > summary->explicit_reference_signature_count) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason
            = "explicit_reference_unresolved_count_invalid";
        return;
    }
    if (summary->explicit_reference_ambiguous_signature_count
        > summary->explicit_reference_signature_count) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason
            = "explicit_reference_ambiguous_count_invalid";
        return;
    }
    if (summary->explicit_reference_unresolved_signature_count
        > (summary->explicit_reference_signature_count
           - summary->explicit_reference_ambiguous_signature_count)) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "explicit_reference_consistency_invalid";
        return;
    }
    if (summary->signature_linked == 0U) {
        evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
        evaluation->profile_reason = "signature_unlinked";
        return;
    }
    if (require_resolved_explicit_references
        && summary->explicit_reference_signature_count != 0U) {
        if (summary->explicit_reference_unresolved_signature_count != 0U) {
            evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
            evaluation->profile_reason = "explicit_reference_unresolved";
            return;
        }
        if (summary->explicit_reference_ambiguous_signature_count != 0U) {
            evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_FAIL;
            evaluation->profile_reason = "explicit_reference_ambiguous";
            return;
        }
    }

    evaluation->profile_status = OMC_C2PA_VERIFY_DETAIL_PASS;
    evaluation->profile_reason = "ok";
}
#endif

static int
omc_jumbf_find_indexed_segment_prefix(omc_const_bytes key, const char* marker,
                                      char* out, omc_u32 out_cap,
                                      omc_u32* out_len)
{
    omc_u32 marker_pos;
    omc_u32 marker_len;
    omc_u32 pos;

    if (out == (char*)0 || out_len == (omc_u32*)0 || out_cap == 0U
        || marker == (const char*)0) {
        return 0;
    }
    if (!omc_jumbf_view_find(key, marker, &marker_pos)) {
        return 0;
    }
    marker_len = omc_jumbf_cstr_size(marker);
    pos = marker_pos + marker_len;
    if (pos >= key.size || !omc_jumbf_is_digit(key.data[pos])) {
        return 0;
    }
    while (pos < key.size && key.data[pos] != (omc_u8)']') {
        if (!omc_jumbf_is_digit(key.data[pos])) {
            return 0;
        }
        pos += 1U;
    }
    if (pos >= key.size || key.data[pos] != (omc_u8)']' || pos + 1U >= out_cap) {
        return 0;
    }
    memcpy(out, key.data, pos + 1U);
    out[pos + 1U] = '\0';
    *out_len = pos + 1U;
    return 1;
}

static int
omc_jumbf_key_matches_field(omc_const_bytes key, const char* prefix,
                            omc_u32 prefix_len, const char* field)
{
    omc_u32 field_len;
    omc_u32 full_len;

    field_len = omc_jumbf_cstr_size(field);
    full_len = prefix_len + 1U + field_len;
    if (key.data == (const omc_u8*)0 || prefix == (const char*)0
        || field == (const char*)0 || key.size < full_len) {
        return 0;
    }
    if (memcmp(key.data, prefix, prefix_len) != 0
        || key.data[prefix_len] != (omc_u8)'.'
        || memcmp(key.data + prefix_len + 1U, field, field_len) != 0) {
        return 0;
    }
    if (key.size == full_len) {
        return 1;
    }
    return omc_jumbf_path_separator(key.data[full_len]);
}

static int
omc_jumbf_key_matches_indexed_field_suffix(omc_const_bytes key,
                                           const char* prefix,
                                           omc_u32 prefix_len,
                                           const char* marker,
                                           const char* suffix)
{
    omc_u32 pos;
    omc_u32 marker_len;
    omc_u32 suffix_len;

    if (key.data == (const omc_u8*)0 || prefix == (const char*)0
        || marker == (const char*)0 || suffix == (const char*)0
        || key.size <= prefix_len + 1U) {
        return 0;
    }
    if (memcmp(key.data, prefix, prefix_len) != 0
        || key.data[prefix_len] != (omc_u8)'.') {
        return 0;
    }

    marker_len = omc_jumbf_cstr_size(marker);
    suffix_len = omc_jumbf_cstr_size(suffix);
    pos = prefix_len + 1U;
    if (pos + marker_len + 4U + suffix_len > key.size
        || memcmp(key.data + pos, marker, marker_len) != 0) {
        return 0;
    }
    pos += marker_len;
    if (pos >= key.size || key.data[pos] != (omc_u8)'[') {
        return 0;
    }
    pos += 1U;
    if (pos >= key.size || !omc_jumbf_is_digit(key.data[pos])) {
        return 0;
    }
    while (pos < key.size && omc_jumbf_is_digit(key.data[pos])) {
        pos += 1U;
    }
    if (pos + 2U + suffix_len != key.size || key.data[pos] != (omc_u8)']'
        || key.data[pos + 1U] != (omc_u8)'.') {
        return 0;
    }
    return memcmp(key.data + pos + 2U, suffix, suffix_len) == 0;
}

static int
omc_jumbf_key_is_indexed_item(omc_const_bytes key, const char* prefix,
                              omc_u32 prefix_len, omc_u32 index)
{
    omc_u32 pos;
    omc_u64 parsed;
    int have_digit;

    if (key.data == (const omc_u8*)0 || prefix == (const char*)0
        || key.size < prefix_len + 3U) {
        return 0;
    }
    if (memcmp(key.data, prefix, prefix_len) != 0
        || key.data[prefix_len] != (omc_u8)'[') {
        return 0;
    }
    pos = prefix_len + 1U;
    parsed = 0U;
    have_digit = 0;
    while (pos < key.size && omc_jumbf_is_digit(key.data[pos])) {
        have_digit = 1;
        parsed = parsed * 10U + (omc_u64)(key.data[pos] - (omc_u8)'0');
        if (parsed > (omc_u64)(~(omc_u32)0)) {
            return 0;
        }
        pos += 1U;
    }
    if (!have_digit || pos >= key.size || key.data[pos] != (omc_u8)']') {
        return 0;
    }
    pos += 1U;
    return pos == key.size && parsed == (omc_u64)index;
}

static omc_const_bytes
omc_jumbf_entry_key_view(const omc_store* store, const omc_entry* entry)
{
    if (store == (const omc_store*)0 || entry == (const omc_entry*)0) {
        return omc_jumbf_const_bytes((const void*)0, 0U);
    }
    if (entry->key.kind == OMC_KEY_JUMBF_CBOR_KEY) {
        return omc_arena_view(&store->arena, entry->key.u.jumbf_cbor_key.key);
    }
    if (entry->key.kind == OMC_KEY_JUMBF_FIELD) {
        return omc_arena_view(&store->arena, entry->key.u.jumbf_field.field);
    }
    return omc_jumbf_const_bytes((const void*)0, 0U);
}

static omc_const_bytes
omc_jumbf_entry_value_view(const omc_store* store, const omc_entry* entry)
{
    if (store == (const omc_store*)0 || entry == (const omc_entry*)0) {
        return omc_jumbf_const_bytes((const void*)0, 0U);
    }
    if (entry->value.kind == OMC_VAL_TEXT || entry->value.kind == OMC_VAL_BYTES) {
        return omc_arena_view(&store->arena, entry->value.u.ref);
    }
    return omc_jumbf_const_bytes((const void*)0, 0U);
}

static int
omc_jumbf_parse_decimal_u32(const char* text, omc_u32 begin, omc_u32 end,
                            omc_u32* out_value);

static int
omc_jumbf_extract_box_payload_for_cbor_prefix(omc_jumbf_ctx* ctx,
                                              omc_size scan_limit,
                                              const char* cbor_prefix,
                                              omc_u32 cbor_prefix_len,
                                              const omc_u8** out_data,
                                              omc_u64* out_size)
{
    omc_u64 off;
    omc_u64 end;
    omc_u32 pos;
    omc_jumbf_box box;

    if (ctx == (omc_jumbf_ctx*)0
        || ctx->input == (const omc_u8*)0 || cbor_prefix == (const char*)0
        || out_data == (const omc_u8**)0 || out_size == (omc_u64*)0
        || cbor_prefix_len <= 5U
        || memcmp(cbor_prefix + cbor_prefix_len - 5U, ".cbor", 5U) != 0
        || memcmp(cbor_prefix, "box.", 4U) != 0) {
        return 0;
    }
    (void)scan_limit;

    off = 0U;
    end = (omc_u64)ctx->input_size;
    pos = 4U;
    while (pos < cbor_prefix_len - 5U) {
        omc_u32 idx;
        omc_u32 end_pos;
        omc_u32 path_end;
        omc_u64 child_off;
        omc_u32 child_count;
        int found;

        path_end = cbor_prefix_len - 5U;
        end_pos = pos;
        while (end_pos < path_end && cbor_prefix[end_pos] != '.') {
            end_pos += 1U;
        }
        if (!omc_jumbf_parse_decimal_u32(cbor_prefix, pos, end_pos, &idx)) {
            return 0;
        }
        if (end_pos < path_end) {
            if (cbor_prefix[end_pos] != '.') {
                return 0;
            }
            pos = end_pos + 1U;
        } else {
            pos = end_pos;
        }

        child_off = off;
        child_count = 0U;
        found = 0;
        while (child_off < end) {
            if (!omc_jumbf_parse_box(ctx->input, ctx->input_size, child_off, end,
                                     &box)
                || box.size == 0U) {
                return 0;
            }
            if (child_count == idx) {
                found = 1;
                break;
            }
            child_off += box.size;
            child_count += 1U;
        }
        if (!found) {
            return 0;
        }
        off = box.offset + box.header_size;
        end = box.offset + box.size;
    }

    if (box.header_size > box.size || off > end
        || end > (omc_u64)ctx->input_size) {
        return 0;
    }
    *out_data = ctx->input + off;
    *out_size = end - off;
    return 1;
}

static int
omc_jumbf_find_semantic_claim_prefix_by_bytes(omc_jumbf_ctx* ctx,
                                              omc_size scan_limit,
                                              const omc_u8* claim_bytes,
                                              omc_u64 claim_size,
                                              char* out, omc_u32 out_cap,
                                              omc_u32* out_len,
                                              omc_u32* out_matches)
{
    omc_size i;
    omc_u32 match_state;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || claim_bytes == (const omc_u8*)0 || out == (char*)0
        || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }

    match_state = 0U;
    out[0] = '\0';
    *out_len = 0U;
    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        omc_const_bytes value;
        omc_u32 prefix_len;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY
            || entry->value.kind != OMC_VAL_BYTES) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        value = omc_jumbf_entry_value_view(ctx->store, entry);
        if (key.size < 6U
            || memcmp(key.data + key.size - 6U, ".claim", 6U) != 0
            || value.size != claim_size
            || memcmp(value.data, claim_bytes, (omc_size)claim_size) != 0) {
            continue;
        }

        prefix_len = (omc_u32)(key.size - 6U);
        if (prefix_len + 1U > out_cap) {
            return 0;
        }
        if (match_state == 0U) {
            memcpy(out, key.data, prefix_len);
            out[prefix_len] = '\0';
            *out_len = prefix_len;
            match_state = 1U;
        } else if (*out_len != prefix_len
                   || memcmp(out, key.data, prefix_len) != 0) {
            match_state = 2U;
        }
    }
    if (out_matches != (omc_u32*)0) {
        *out_matches = match_state;
    }
    return match_state != 0U;
}

static int
omc_jumbf_prefix_seen_before(const omc_store* store, omc_block_id block,
                             omc_size limit, omc_size before_index,
                             const char* marker, const char* prefix,
                             omc_u32 prefix_len)
{
    omc_size i;

    if (store == (const omc_store*)0 || prefix == (const char*)0) {
        return 0;
    }
    for (i = 0U; i < before_index && i < limit; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char other_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 other_prefix_len;

        entry = &store->entries[i];
        if (entry->origin.block != block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(store, entry);
        if (!omc_jumbf_find_indexed_segment_prefix(key, marker, other_prefix,
                                                   (omc_u32)sizeof(other_prefix),
                                                   &other_prefix_len)) {
            continue;
        }
        if (other_prefix_len == prefix_len
            && memcmp(other_prefix, prefix, prefix_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
omc_jumbf_extract_manifest_prefix_from_key(omc_const_bytes key, char* out,
                                           omc_u32 out_cap,
                                           omc_u32* out_len);

static int
omc_jumbf_manifest_prefix_seen_before(const omc_store* store,
                                      omc_block_id block, omc_size limit,
                                      omc_size before_index,
                                      const char* prefix,
                                      omc_u32 prefix_len)
{
    omc_size i;

    if (store == (const omc_store*)0 || prefix == (const char*)0) {
        return 0;
    }
    for (i = 0U; i < before_index && i < limit; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char other_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 other_prefix_len;

        entry = &store->entries[i];
        if (entry->origin.block != block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(store, entry);
        if (!omc_jumbf_extract_manifest_prefix_from_key(key, other_prefix,
                                                        (omc_u32)sizeof(other_prefix),
                                                        &other_prefix_len)) {
            continue;
        }
        if (other_prefix_len == prefix_len
            && memcmp(other_prefix, prefix, prefix_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static void
omc_jumbf_ing_proj_init(omc_jumbf_ing_proj* proj)
{
    if (proj == (omc_jumbf_ing_proj*)0) {
        return;
    }
    memset(proj, 0, sizeof(*proj));
}

static int
omc_jumbf_collect_ingredient_projection(omc_jumbf_ctx* ctx, omc_size scan_limit,
                                        const char* prefix, omc_u32 prefix_len,
                                        omc_jumbf_ing_proj* out_proj)
{
    omc_size i;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || prefix == (const char*)0
        || out_proj == (omc_jumbf_ing_proj*)0
        || prefix_len + 1U >= (omc_u32)sizeof(out_proj->prefix)) {
        return 0;
    }

    omc_jumbf_ing_proj_init(out_proj);
    memcpy(out_proj->prefix, prefix, prefix_len);
    out_proj->prefix[prefix_len] = '\0';
    out_proj->prefix_len = prefix_len;

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        omc_const_bytes value_view;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (!omc_jumbf_view_starts_with(key, prefix)
            || (key.size != prefix_len
                && key.data[prefix_len] != (omc_u8)'.'
                && key.data[prefix_len] != (omc_u8)'[')) {
            continue;
        }

        out_proj->key_hits += 1U;
        value_view = omc_jumbf_entry_value_view(ctx->store, entry);

        if (!out_proj->has_title && entry->value.kind == OMC_VAL_TEXT
            && omc_jumbf_key_matches_field(key, prefix, prefix_len, "title")
            && value_view.size < (omc_size)sizeof(out_proj->title)) {
            memcpy(out_proj->title, value_view.data, value_view.size);
            out_proj->title[value_view.size] = '\0';
            out_proj->title_len = (omc_u32)value_view.size;
            out_proj->has_title = (out_proj->title_len != 0U);
        }
        if (!out_proj->has_relationship
            && entry->value.kind == OMC_VAL_TEXT
            && omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                           "relationship")
            && value_view.size < (omc_size)sizeof(out_proj->relationship)
            && omc_jumbf_bytes_all_ascii_printable(
                   value_view.data, (omc_u32)value_view.size)) {
            memcpy(out_proj->relationship, value_view.data, value_view.size);
            out_proj->relationship[value_view.size] = '\0';
            out_proj->relationship_len = (omc_u32)value_view.size;
            out_proj->has_relationship = (out_proj->relationship_len != 0U);
        }
        if (!out_proj->has_thumbnail_url
            && entry->value.kind == OMC_VAL_TEXT
            && omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                           "thumbnailUrl")
            && value_view.size < (omc_size)sizeof(out_proj->thumbnail_url)) {
            memcpy(out_proj->thumbnail_url, value_view.data, value_view.size);
            out_proj->thumbnail_url[value_view.size] = '\0';
            out_proj->thumbnail_url_len = (omc_u32)value_view.size;
            out_proj->has_thumbnail_url
                = (out_proj->thumbnail_url_len != 0U);
        }
    }
    return 1;
}

static int
omc_jumbf_emit_claim_ingredient_projection(omc_jumbf_ctx* ctx,
                                           const char* root, omc_u32 index,
                                           const omc_jumbf_ing_proj* ingredient)
{
    if (ctx == (omc_jumbf_ctx*)0 || root == (const char*)0
        || ingredient == (const omc_jumbf_ing_proj*)0) {
        return 0;
    }
    if (!omc_jumbf_emit_indexed_field_text(ctx, root, index, "prefix",
                                           ingredient->prefix,
                                           ingredient->prefix_len)
        || !omc_jumbf_emit_indexed_field_u64(ctx, root, index, "key_hits",
                                             ingredient->key_hits)) {
        return 0;
    }
    if (ingredient->has_title
        && !omc_jumbf_emit_indexed_field_text(ctx, root, index, "title",
                                              ingredient->title,
                                              ingredient->title_len)) {
        return 0;
    }
    if (ingredient->has_relationship
        && !omc_jumbf_emit_indexed_field_text(
               ctx, root, index, "relationship", ingredient->relationship,
               ingredient->relationship_len)) {
        return 0;
    }
    if (ingredient->has_thumbnail_url
        && !omc_jumbf_emit_indexed_field_text(ctx, root, index,
                                              "thumbnail_url",
                                              ingredient->thumbnail_url,
                                              ingredient->thumbnail_url_len)) {
        return 0;
    }
    return 1;
}

static void
omc_jumbf_ing_summary_init(omc_jumbf_ing_summary* summary)
{
    if (summary == (omc_jumbf_ing_summary*)0) {
        return;
    }
    memset(summary, 0, sizeof(*summary));
}

static void
omc_jumbf_ing_summary_note_relationship(omc_jumbf_ing_summary* summary,
                                        const char* name, omc_u32 name_len)
{
    omc_u32 i;

    if (summary == (omc_jumbf_ing_summary*)0 || name == (const char*)0
        || name_len == 0U) {
        return;
    }
    for (i = 0U; i < summary->named_count_len; ++i) {
        if (summary->named_counts[i].name_len == name_len
            && memcmp(summary->named_counts[i].name, name, name_len) == 0) {
            summary->named_counts[i].count += 1U;
            return;
        }
    }
    if (summary->named_count_len >= OMC_JUMBF_MAX_REL_KINDS
        || name_len + 1U > (omc_u32)sizeof(summary->named_counts[0].name)) {
        return;
    }
    memcpy(summary->named_counts[summary->named_count_len].name, name,
           name_len);
    summary->named_counts[summary->named_count_len].name[name_len] = '\0';
    summary->named_counts[summary->named_count_len].name_len = name_len;
    summary->named_counts[summary->named_count_len].count = 1U;
    summary->named_count_len += 1U;
    summary->relationship_kind_count = summary->named_count_len;
}

static int
omc_jumbf_collect_claim_ingredient_summary(omc_jumbf_ctx* ctx,
                                           omc_size scan_limit,
                                           const char* claim_prefix,
                                           omc_u32 claim_prefix_len,
                                           omc_jumbf_ing_summary* out_summary)
{
    omc_size i;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || claim_prefix == (const char*)0
        || out_summary == (omc_jumbf_ing_summary*)0) {
        return 0;
    }

    omc_jumbf_ing_summary_init(out_summary);
    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char ingredient_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 ingredient_prefix_len;
        omc_jumbf_ing_proj ingredient;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (!omc_jumbf_find_indexed_segment_prefix(key, ".ingredients[",
                                                   ingredient_prefix,
                                                   (omc_u32)sizeof(ingredient_prefix),
                                                   &ingredient_prefix_len)
            || !omc_jumbf_prefix_is_nested_under(ingredient_prefix,
                                                 ingredient_prefix_len,
                                                 claim_prefix,
                                                 claim_prefix_len)
            || omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                            i, ".ingredients[",
                                            ingredient_prefix,
                                            ingredient_prefix_len)) {
            continue;
        }
        if (!omc_jumbf_collect_ingredient_projection(ctx, scan_limit,
                                                     ingredient_prefix,
                                                     ingredient_prefix_len,
                                                     &ingredient)) {
            return 0;
        }
        out_summary->ingredient_count += 1U;
        if (ingredient.has_title) {
            out_summary->title_count += 1U;
        }
        if (ingredient.has_relationship) {
            out_summary->relationship_count += 1U;
            omc_jumbf_ing_summary_note_relationship(
                out_summary, ingredient.relationship,
                ingredient.relationship_len);
        }
        if (ingredient.has_thumbnail_url) {
            out_summary->thumbnail_url_count += 1U;
        }
    }
    return 1;
}

static int
omc_jumbf_emit_named_counts(omc_jumbf_ctx* ctx, const char* root,
                            const char* stem,
                            const omc_jumbf_ing_summary* summary)
{
    char field[OMC_JUMBF_PATH_CAP];
    omc_u32 len;
    omc_u32 i;

    if (ctx == (omc_jumbf_ctx*)0 || root == (const char*)0
        || stem == (const char*)0
        || summary == (const omc_jumbf_ing_summary*)0) {
        return 0;
    }

    len = 0U;
    field[0] = '\0';
    if (!omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, stem,
                                 omc_jumbf_cstr_size(stem))
        || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len,
                                 "_kind_count", 11U)
        || !omc_jumbf_emit_field_u64(ctx, field, len,
                                     (omc_u64)summary->relationship_kind_count)) {
        return 0;
    }

    for (i = 0U; i < summary->named_count_len; ++i) {
        len = 0U;
        field[0] = '\0';
        if (!omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, root,
                                  omc_jumbf_cstr_size(root))
            || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len,
                                      '.')
            || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, stem,
                                     omc_jumbf_cstr_size(stem))
            || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len,
                                      '.')
            || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len,
                                     summary->named_counts[i].name,
                                     summary->named_counts[i].name_len)
            || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len,
                                     "_count", 6U)
            || !omc_jumbf_emit_field_u64(ctx, field, len,
                                         summary->named_counts[i].count)) {
            return 0;
        }
    }
    return 1;
}

static int
omc_jumbf_emit_scoped_field_u64(omc_jumbf_ctx* ctx, const char* root,
                                const char* field, omc_u64 value)
{
    char full_key[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    if (ctx == (omc_jumbf_ctx*)0 || root == (const char*)0
        || field == (const char*)0) {
        return 0;
    }

    len = 0U;
    full_key[0] = '\0';
    if (!omc_jumbf_append_mem(full_key, (omc_u32)sizeof(full_key), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(full_key, (omc_u32)sizeof(full_key), &len,
                                  '.')
        || !omc_jumbf_append_mem(full_key, (omc_u32)sizeof(full_key), &len,
                                 field, omc_jumbf_cstr_size(field))
        || !omc_jumbf_emit_field_u64(ctx, full_key, len, value)) {
        return 0;
    }
    return 1;
}

static void
omc_jumbf_ing_summary_merge(omc_jumbf_ing_summary* dst,
                            const omc_jumbf_ing_summary* src)
{
    omc_u32 i;

    if (dst == (omc_jumbf_ing_summary*)0 || src == (const omc_jumbf_ing_summary*)0) {
        return;
    }
    dst->ingredient_count += src->ingredient_count;
    dst->title_count += src->title_count;
    dst->relationship_count += src->relationship_count;
    dst->thumbnail_url_count += src->thumbnail_url_count;
    for (i = 0U; i < src->named_count_len; ++i) {
        omc_u32 add_count;

        add_count = 0U;
        while ((omc_u64)add_count < src->named_counts[i].count) {
            omc_jumbf_ing_summary_note_relationship(dst,
                                                    src->named_counts[i].name,
                                                    src->named_counts[i].name_len);
            add_count += 1U;
        }
    }
}

static void
omc_jumbf_exp_ref_scope_stats_init(omc_jumbf_exp_ref_scope_stats* stats)
{
    if (stats == (omc_jumbf_exp_ref_scope_stats*)0) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

static void
omc_jumbf_exp_ref_scope_stats_note_claim(
    omc_jumbf_exp_ref_scope_stats* stats,
    const omc_jumbf_claim_link_stats* claim_link_stats)
{
    int any_direct;
    int any_cross;
    int any_unresolved_direct;
    int any_unresolved_cross;
    int any_ambiguous_direct;
    int any_ambiguous_cross;

    if (stats == (omc_jumbf_exp_ref_scope_stats*)0
        || claim_link_stats == (const omc_jumbf_claim_link_stats*)0) {
        return;
    }

    any_direct
        = claim_link_stats->linked_explicit_reference_direct_signature_count
          != 0U;
    any_cross
        = claim_link_stats->linked_explicit_reference_cross_signature_count
          != 0U;
    if (claim_link_stats->linked_explicit_reference_signature_count != 0U) {
        stats->claim_count += 1U;
        stats->direct_source_count
            += claim_link_stats
                   ->linked_explicit_reference_direct_signature_count;
        stats->cross_source_count
            += claim_link_stats
                   ->linked_explicit_reference_cross_signature_count;
        omc_jumbf_ing_summary_merge(&stats->summary,
                                    &claim_link_stats
                                         ->linked_explicit_reference_summary);
        if (any_direct) {
            stats->direct_claim_count += 1U;
            omc_jumbf_ing_summary_merge(
                &stats->direct_summary,
                &claim_link_stats
                     ->linked_explicit_reference_direct_summary);
        }
        if (any_cross) {
            stats->cross_claim_count += 1U;
            omc_jumbf_ing_summary_merge(
                &stats->cross_summary,
                &claim_link_stats
                     ->linked_explicit_reference_cross_summary);
        }
        if (any_direct && any_cross) {
            stats->mixed_source_count += 1U;
        }
    }

    any_unresolved_direct
        = claim_link_stats
              ->linked_explicit_reference_unresolved_direct_signature_count
          != 0U;
    any_unresolved_cross
        = claim_link_stats
              ->linked_explicit_reference_unresolved_cross_signature_count
          != 0U;
    if (claim_link_stats->linked_explicit_reference_unresolved_signature_count
        != 0U) {
        stats->unresolved_claim_count += 1U;
        stats->unresolved_direct_source_count
            += claim_link_stats
                   ->linked_explicit_reference_unresolved_direct_signature_count;
        stats->unresolved_cross_source_count
            += claim_link_stats
                   ->linked_explicit_reference_unresolved_cross_signature_count;
        omc_jumbf_ing_summary_merge(
            &stats->unresolved_summary,
            &claim_link_stats->linked_explicit_reference_unresolved_summary);
        if (any_unresolved_direct) {
            stats->unresolved_direct_claim_count += 1U;
            omc_jumbf_ing_summary_merge(
                &stats->unresolved_direct_summary,
                &claim_link_stats
                     ->linked_explicit_reference_unresolved_direct_summary);
        }
        if (any_unresolved_cross) {
            stats->unresolved_cross_claim_count += 1U;
            omc_jumbf_ing_summary_merge(
                &stats->unresolved_cross_summary,
                &claim_link_stats
                     ->linked_explicit_reference_unresolved_cross_summary);
        }
        if (any_unresolved_direct && any_unresolved_cross) {
            stats->unresolved_mixed_source_count += 1U;
        }
    }

    any_ambiguous_direct
        = claim_link_stats
              ->linked_explicit_reference_ambiguous_direct_signature_count
          != 0U;
    any_ambiguous_cross
        = claim_link_stats
              ->linked_explicit_reference_ambiguous_cross_signature_count
          != 0U;
    if (claim_link_stats->linked_explicit_reference_ambiguous_signature_count
        != 0U) {
        stats->ambiguous_claim_count += 1U;
        stats->ambiguous_direct_source_count
            += claim_link_stats
                   ->linked_explicit_reference_ambiguous_direct_signature_count;
        stats->ambiguous_cross_source_count
            += claim_link_stats
                   ->linked_explicit_reference_ambiguous_cross_signature_count;
        omc_jumbf_ing_summary_merge(
            &stats->ambiguous_summary,
            &claim_link_stats->linked_explicit_reference_ambiguous_summary);
        if (any_ambiguous_direct) {
            stats->ambiguous_direct_claim_count += 1U;
            omc_jumbf_ing_summary_merge(
                &stats->ambiguous_direct_summary,
                &claim_link_stats
                     ->linked_explicit_reference_ambiguous_direct_summary);
        }
        if (any_ambiguous_cross) {
            stats->ambiguous_cross_claim_count += 1U;
            omc_jumbf_ing_summary_merge(
                &stats->ambiguous_cross_summary,
                &claim_link_stats
                     ->linked_explicit_reference_ambiguous_cross_summary);
        }
        if (any_ambiguous_direct && any_ambiguous_cross) {
            stats->ambiguous_mixed_source_count += 1U;
        }
    }
}

static int
omc_jumbf_emit_exp_ref_scope_fields(
    omc_jumbf_ctx* ctx, const char* root,
    const omc_jumbf_exp_ref_scope_stats* stats)
{
    if (ctx == (omc_jumbf_ctx*)0 || root == (const char*)0
        || stats == (const omc_jumbf_exp_ref_scope_stats*)0) {
        return 0;
    }

    if (!omc_jumbf_emit_scoped_field_u64(
            ctx, root, "ingredient_linked_claim_explicit_reference_count",
            stats->claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_direct_source_count",
               stats->direct_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_cross_source_count",
               stats->cross_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_mixed_source_count",
               stats->mixed_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_unresolved_count",
               stats->unresolved_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_unresolved_"
               "direct_source_count",
               stats->unresolved_direct_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_unresolved_"
               "cross_source_count",
               stats->unresolved_cross_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_unresolved_"
               "mixed_source_count",
               stats->unresolved_mixed_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_ambiguous_count",
               stats->ambiguous_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_ambiguous_"
               "direct_source_count",
               stats->ambiguous_direct_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_ambiguous_"
               "cross_source_count",
               stats->ambiguous_cross_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_claim_explicit_reference_ambiguous_"
               "mixed_source_count",
               stats->ambiguous_mixed_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_direct_"
               "claim_count",
               stats->direct_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_cross_"
               "claim_count",
               stats->cross_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_direct_"
               "source_count",
               stats->direct_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_cross_"
               "source_count",
               stats->cross_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_mixed_"
               "source_count",
               stats->mixed_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_direct_"
               "title_count",
               stats->direct_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_cross_"
               "title_count",
               stats->cross_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_direct_"
               "relationship_count",
               stats->direct_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_cross_"
               "relationship_count",
               stats->cross_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_direct_"
               "thumbnail_url_count",
               stats->direct_summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_cross_"
               "thumbnail_url_count",
               stats->cross_summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_title_count",
               stats->summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_"
               "relationship_count",
               stats->summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_"
               "thumbnail_url_count",
               stats->summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "direct_claim_count",
               stats->unresolved_direct_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "cross_claim_count",
               stats->unresolved_cross_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "direct_source_count",
               stats->unresolved_direct_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "cross_source_count",
               stats->unresolved_cross_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "mixed_source_count",
               stats->unresolved_mixed_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "direct_title_count",
               stats->unresolved_direct_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "cross_title_count",
               stats->unresolved_cross_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "direct_relationship_count",
               stats->unresolved_direct_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "cross_relationship_count",
               stats->unresolved_cross_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "direct_thumbnail_url_count",
               stats->unresolved_direct_summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "cross_thumbnail_url_count",
               stats->unresolved_cross_summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "title_count",
               stats->unresolved_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "relationship_count",
               stats->unresolved_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_unresolved_"
               "thumbnail_url_count",
               stats->unresolved_summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "direct_claim_count",
               stats->ambiguous_direct_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "cross_claim_count",
               stats->ambiguous_cross_claim_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "direct_source_count",
               stats->ambiguous_direct_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "cross_source_count",
               stats->ambiguous_cross_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "mixed_source_count",
               stats->ambiguous_mixed_source_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "direct_title_count",
               stats->ambiguous_direct_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "cross_title_count",
               stats->ambiguous_cross_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "direct_relationship_count",
               stats->ambiguous_direct_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "cross_relationship_count",
               stats->ambiguous_cross_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "direct_thumbnail_url_count",
               stats->ambiguous_direct_summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "cross_thumbnail_url_count",
               stats->ambiguous_cross_summary.thumbnail_url_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "title_count",
               stats->ambiguous_summary.title_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "relationship_count",
               stats->ambiguous_summary.relationship_count)
        || !omc_jumbf_emit_scoped_field_u64(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_ambiguous_"
               "thumbnail_url_count",
               stats->ambiguous_summary.thumbnail_url_count)
        || !omc_jumbf_emit_named_counts(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_relationship",
               &stats->summary)
        || !omc_jumbf_emit_named_counts(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_"
               "unresolved_relationship",
               &stats->unresolved_summary)
        || !omc_jumbf_emit_named_counts(
               ctx, root,
               "ingredient_linked_signature_explicit_reference_"
               "ambiguous_relationship",
               &stats->ambiguous_summary)) {
        return 0;
    }
    return 1;
}

static int
omc_jumbf_emit_scoped_field_u8(omc_jumbf_ctx* ctx, const char* root,
                               const char* field, omc_u8 value)
{
    char full_key[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    if (ctx == (omc_jumbf_ctx*)0 || root == (const char*)0
        || field == (const char*)0) {
        return 0;
    }

    len = 0U;
    full_key[0] = '\0';
    if (!omc_jumbf_append_mem(full_key, (omc_u32)sizeof(full_key), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(full_key, (omc_u32)sizeof(full_key), &len,
                                  '.')
        || !omc_jumbf_append_mem(full_key, (omc_u32)sizeof(full_key), &len,
                                 field, omc_jumbf_cstr_size(field))
        || !omc_jumbf_emit_field_u8(ctx, full_key, len, value)) {
        return 0;
    }
    return 1;
}

static int
omc_jumbf_emit_scoped_field_text(omc_jumbf_ctx* ctx, const char* root,
                                 const char* field, const char* value,
                                 omc_u32 value_len)
{
    char full_key[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    if (ctx == (omc_jumbf_ctx*)0 || root == (const char*)0
        || field == (const char*)0 || value == (const char*)0) {
        return 0;
    }

    len = 0U;
    full_key[0] = '\0';
    if (!omc_jumbf_append_mem(full_key, (omc_u32)sizeof(full_key), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(full_key, (omc_u32)sizeof(full_key), &len,
                                  '.')
        || !omc_jumbf_append_mem(full_key, (omc_u32)sizeof(full_key), &len,
                                 field, omc_jumbf_cstr_size(field))
        || !omc_jumbf_emit_field_text(ctx, full_key, len, value, value_len)) {
        return 0;
    }
    return 1;
}

static omc_u64
omc_jumbf_count_keys_in_prefix(const omc_store* store, omc_block_id block,
                               omc_size limit, const char* prefix,
                               omc_u32 prefix_len)
{
    omc_size i;
    omc_u64 count;

    if (store == (const omc_store*)0 || prefix == (const char*)0) {
        return 0U;
    }

    count = 0U;
    for (i = 0U; i < limit && i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;

        entry = &store->entries[i];
        if (entry->origin.block != block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(store, entry);
        if (!omc_jumbf_view_starts_with(key, prefix)) {
            continue;
        }
        if (key.size != prefix_len && key.data[prefix_len] != (omc_u8)'.'
            && key.data[prefix_len] != (omc_u8)'[') {
            continue;
        }
        count += 1U;
    }
    return count;
}

static omc_u64
omc_jumbf_count_nested_indexed_prefixes(const omc_store* store,
                                        omc_block_id block, omc_size limit,
                                        const char* parent_prefix,
                                        omc_u32 parent_prefix_len,
                                        const char* marker)
{
    omc_size i;
    omc_u64 count;

    if (store == (const omc_store*)0 || marker == (const char*)0) {
        return 0U;
    }

    count = 0U;
    for (i = 0U; i < limit && i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 prefix_len;

        entry = &store->entries[i];
        if (entry->origin.block != block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(store, entry);
        if (!omc_jumbf_find_indexed_segment_prefix(
                key, marker, prefix, (omc_u32)sizeof(prefix), &prefix_len)
            || (parent_prefix != (const char*)0 && parent_prefix_len != 0U
                && !omc_jumbf_prefix_is_nested_under(
                       prefix, prefix_len, parent_prefix, parent_prefix_len))
            || omc_jumbf_prefix_seen_before(store, block, limit, i, marker,
                                            prefix, prefix_len)) {
            continue;
        }
        count += 1U;
    }
    return count;
}

static int
omc_jumbf_build_indexed_root(const char* root, omc_u32 index,
                             const char* child, char* out, omc_u32 out_cap,
                             omc_u32* out_len)
{
    omc_u32 len;

    if (root == (const char*)0 || child == (const char*)0 || out == (char*)0
        || out_len == (omc_u32*)0) {
        return 0;
    }
    len = 0U;
    out[0] = '\0';
    if (!omc_jumbf_append_mem(out, out_cap, &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(out, out_cap, &len, '.')
        || !omc_jumbf_append_u32_dec(out, out_cap, &len, index)
        || !omc_jumbf_append_char(out, out_cap, &len, '.')
        || !omc_jumbf_append_mem(out, out_cap, &len, child,
                                 omc_jumbf_cstr_size(child))) {
        return 0;
    }
    *out_len = len;
    return 1;
}

static int
omc_jumbf_build_indexed_scope(const char* root, omc_u32 index, char* out,
                              omc_u32 out_cap, omc_u32* out_len)
{
    omc_u32 len;

    if (root == (const char*)0 || out == (char*)0 || out_len == (omc_u32*)0) {
        return 0;
    }
    len = 0U;
    out[0] = '\0';
    if (!omc_jumbf_append_mem(out, out_cap, &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(out, out_cap, &len, '.')
        || !omc_jumbf_append_u32_dec(out, out_cap, &len, index)) {
        return 0;
    }
    *out_len = len;
    return 1;
}

static void
omc_jumbf_fourcc_text(omc_u32 type, char out[5])
{
    omc_u32 i;
    omc_u8 bytes[4];

    bytes[0] = (omc_u8)((type >> 24) & 0xFFU);
    bytes[1] = (omc_u8)((type >> 16) & 0xFFU);
    bytes[2] = (omc_u8)((type >> 8) & 0xFFU);
    bytes[3] = (omc_u8)((type >> 0) & 0xFFU);

    for (i = 0U; i < 4U; ++i) {
        out[i] = omc_jumbf_is_printable_ascii(bytes[i]) ? (char)bytes[i] : '_';
    }
    out[4] = '\0';
}

static int
omc_jumbf_bytes_valid_utf8(const omc_u8* bytes, omc_u32 size)
{
    omc_u32 index;

    index = 0U;
    while (index < size) {
        omc_u8 c0;
        omc_u32 needed;
        omc_u32 min_cp;
        omc_u32 codepoint;
        omc_u32 j;

        c0 = bytes[index];
        if ((c0 & 0x80U) == 0U) {
            index += 1U;
            continue;
        }

        needed = 0U;
        min_cp = 0U;
        codepoint = 0U;
        if ((c0 & 0xE0U) == 0xC0U) {
            needed = 1U;
            min_cp = 0x80U;
            codepoint = (omc_u32)(c0 & 0x1FU);
        } else if ((c0 & 0xF0U) == 0xE0U) {
            needed = 2U;
            min_cp = 0x800U;
            codepoint = (omc_u32)(c0 & 0x0FU);
        } else if ((c0 & 0xF8U) == 0xF0U) {
            needed = 3U;
            min_cp = 0x10000U;
            codepoint = (omc_u32)(c0 & 0x07U);
        } else {
            return 0;
        }

        if (index + needed >= size) {
            return 0;
        }
        for (j = 0U; j < needed; ++j) {
            omc_u8 cx;

            cx = bytes[index + 1U + j];
            if ((cx & 0xC0U) != 0x80U) {
                return 0;
            }
            codepoint = (codepoint << 6U) | (omc_u32)(cx & 0x3FU);
        }
        if (codepoint < min_cp || codepoint > 0x10FFFFU
            || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return 0;
        }
        index += 1U + needed;
    }
    return 1;
}

static int
omc_jumbf_prefix_is_nested_under(const char* prefix, omc_u32 prefix_len,
                                 const char* parent, omc_u32 parent_len)
{
    if (prefix == (const char*)0 || parent == (const char*)0
        || prefix_len <= parent_len) {
        return 0;
    }
    return memcmp(prefix, parent, parent_len) == 0
           && prefix[parent_len] == '.';
}

static int
omc_jumbf_extract_manifest_prefix_from_claim_prefix(const char* claim_prefix,
                                                    omc_u32 claim_prefix_len,
                                                    char* out, omc_u32 out_cap,
                                                    omc_u32* out_len)
{
    omc_u32 pos;

    if (claim_prefix == (const char*)0 || out == (char*)0
        || out_len == (omc_u32*)0 || out_cap == 0U
        || claim_prefix_len + 1U > out_cap) {
        return 0;
    }
    for (pos = 0U; pos + 8U <= claim_prefix_len; ++pos) {
        if (memcmp(claim_prefix + pos, ".claims[", 8U) == 0) {
            if (pos == 0U) {
                return 0;
            }
            memcpy(out, claim_prefix, pos);
            out[pos] = '\0';
            *out_len = pos;
            return 1;
        }
    }
    return 0;
}

static int
omc_jumbf_extract_manifest_prefix_from_key(omc_const_bytes key, char* out,
                                           omc_u32 out_cap,
                                           omc_u32* out_len)
{
    omc_u32 marker_pos;
    omc_u32 pos;

    if (out == (char*)0 || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }
    if (omc_jumbf_find_indexed_segment_prefix(key, ".manifests[", out, out_cap,
                                              out_len)) {
        return 1;
    }
    if (!omc_jumbf_view_find(key, ".manifests.", &marker_pos)) {
        return 0;
    }
    pos = marker_pos + 11U;
    if (pos >= key.size || pos + 1U >= out_cap) {
        return 0;
    }
    while (pos < key.size && !omc_jumbf_path_separator(key.data[pos])) {
        pos += 1U;
    }
    if (pos <= marker_pos + 11U || pos + 1U > out_cap) {
        return 0;
    }
    memcpy(out, key.data, pos);
    out[pos] = '\0';
    *out_len = pos;
    return 1;
}

static int
omc_jumbf_manifest_prefix_is_active(const char* prefix, omc_u32 prefix_len)
{
    static const char k_suffix[] = ".manifests.active_manifest";
    const omc_u32 suffix_len = (omc_u32)(sizeof(k_suffix) - 1U);

    if (prefix == (const char*)0 || prefix_len < suffix_len) {
        return 0;
    }
    return memcmp(prefix + (prefix_len - suffix_len), k_suffix, suffix_len)
           == 0;
}

static int
omc_jumbf_find_claim_prefix_by_semantic_index(omc_jumbf_ctx* ctx,
                                              omc_size scan_limit,
                                              omc_u32 semantic_index,
                                              char* out, omc_u32 out_cap,
                                              omc_u32* out_len)
{
    omc_size i;
    omc_u32 seen;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || out == (char*)0 || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }
    seen = 0U;
    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 prefix_len;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (!omc_jumbf_find_indexed_segment_prefix(key, ".claims[", prefix,
                                                   (omc_u32)sizeof(prefix),
                                                   &prefix_len)
            || omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                            i, ".claims[", prefix,
                                            prefix_len)) {
            continue;
        }
        if (seen == semantic_index) {
            if (prefix_len + 1U > out_cap) {
                return 0;
            }
            memcpy(out, prefix, prefix_len + 1U);
            *out_len = prefix_len;
            return 1;
        }
        seen += 1U;
    }
    return 0;
}

static int
omc_jumbf_is_ref_token_boundary(char ch)
{
    return ch == '?' || ch == '&' || ch == '#' || ch == ';' || ch == '/'
           || ch == '\\' || ch == ':' || ch == ',' || ch == '.'
           || ch == '[' || ch == ']' || ch == '(' || ch == ')'
           || ch == '{' || ch == '}' || ch == '<' || ch == '>'
           || ch == '"' || ch == '\'' || ch == '='
           || (unsigned char)ch <= 0x20U;
}

static int
omc_jumbf_is_label_token_boundary(char ch)
{
    return ch == '?' || ch == '&' || ch == '#' || ch == ';' || ch == '/'
           || ch == '\\' || ch == ':' || ch == ','
           || ch == '[' || ch == ']' || ch == '(' || ch == ')'
           || ch == '{' || ch == '}' || ch == '<' || ch == '>'
           || ch == '"' || ch == '\'' || ch == '='
           || (unsigned char)ch <= 0x20U;
}

static int
omc_jumbf_parse_decimal_u32(const char* text, omc_u32 begin, omc_u32 end,
                            omc_u32* out_value)
{
    omc_u64 value;
    omc_u32 i;

    if (text == (const char*)0 || out_value == (omc_u32*)0 || begin >= end) {
        return 0;
    }
    value = 0U;
    for (i = begin; i < end; ++i) {
        if (!omc_jumbf_is_digit((omc_u8)text[i])) {
            return 0;
        }
        value = value * 10U + (omc_u64)(text[i] - '0');
        if (value > 0xFFFFFFFFUL) {
            return 0;
        }
    }
    *out_value = (omc_u32)value;
    return 1;
}

static int
omc_jumbf_hex_nibble_value(char ch, omc_u8* out)
{
    if (out == (omc_u8*)0) {
        return 0;
    }
    if (ch >= '0' && ch <= '9') {
        *out = (omc_u8)(ch - '0');
        return 1;
    }
    if (ch >= 'a' && ch <= 'f') {
        *out = (omc_u8)(10 + (ch - 'a'));
        return 1;
    }
    if (ch >= 'A' && ch <= 'F') {
        *out = (omc_u8)(10 + (ch - 'A'));
        return 1;
    }
    return 0;
}

static int
omc_jumbf_uri_percent_decode(const char* text, omc_u32 text_len,
                             char* out, omc_u32 out_cap,
                             omc_u32* out_len)
{
    omc_u32 in_i;
    omc_u32 out_i;

    if (text == (const char*)0 || out == (char*)0 || out_len == (omc_u32*)0
        || out_cap == 0U) {
        return 0;
    }
    out_i = 0U;
    for (in_i = 0U; in_i < text_len; ++in_i) {
        if (text[in_i] == '%' && in_i + 2U < text_len) {
            omc_u8 hi;
            omc_u8 lo;

            if (omc_jumbf_hex_nibble_value(text[in_i + 1U], &hi)
                && omc_jumbf_hex_nibble_value(text[in_i + 2U], &lo)) {
                if (out_i + 1U >= out_cap) {
                    return 0;
                }
                out[out_i++] = (char)((hi << 4U) | lo);
                in_i += 2U;
                continue;
            }
        }
        if (out_i + 1U >= out_cap) {
            return 0;
        }
        out[out_i++] = text[in_i];
    }
    out[out_i] = '\0';
    *out_len = out_i;
    return 1;
}

static int
omc_jumbf_ref_text_parse_index(const char* text, omc_u32 text_len,
                               omc_u32* out_index)
{
    static const char* const k_markers[] = {
        "claims[", "claims/", "claims.", "claim_ref=", "claim-ref=",
        "claimref=", "claim_reference=", "claim-reference=",
        "claimreference=", "claim_ref_id=", "claim-ref-id=",
        "claimreferenceid=", "claim_id=", "claim-id=", "claimid=",
        "claim_index=", "claim-index=", "claimindex=",
        "reference_id=", "referenceid=", "reference_index=",
        "referenceindex=", "ref_id=", "refid=", "ref_index=",
        "refindex=", "claim=", "claims="
    };
    char decoded[256];
    omc_u32 decoded_len;
    omc_u32 begin_trim;
    omc_u32 end_trim;
    omc_u32 i;

    if (text == (const char*)0 || out_index == (omc_u32*)0) {
        return 0;
    }
    if (!omc_jumbf_uri_percent_decode(text, text_len, decoded,
                                      (omc_u32)sizeof(decoded),
                                      &decoded_len)) {
        return 0;
    }

    begin_trim = 0U;
    end_trim = decoded_len;
    while (begin_trim < end_trim
           && (unsigned char)decoded[begin_trim] <= 0x20U) {
        begin_trim += 1U;
    }
    while (end_trim > begin_trim
           && (unsigned char)decoded[end_trim - 1U] <= 0x20U) {
        end_trim -= 1U;
    }
    if (begin_trim >= end_trim) {
        return 0;
    }
    if (omc_jumbf_parse_decimal_u32(decoded, begin_trim, end_trim,
                                    out_index)) {
        return 1;
    }

    for (i = 0U; i < (omc_u32)(sizeof(k_markers) / sizeof(k_markers[0])); ++i) {
        const char* marker;
        omc_u32 marker_len;
        omc_u32 pos;

        marker = k_markers[i];
        marker_len = omc_jumbf_cstr_size(marker);
        pos = begin_trim;
        while (pos + marker_len < end_trim) {
            if (omc_jumbf_ascii_icase_contains((const omc_u8*)(decoded + pos),
                                               end_trim - pos, marker,
                                               marker_len)
                && pos + marker_len < end_trim) {
                omc_u32 start;
                omc_u32 end;

                while (pos + marker_len < end_trim
                       && memcmp(decoded + pos, marker, marker_len) != 0) {
                    pos += 1U;
                }
                if (pos + marker_len >= end_trim) {
                    break;
                }
                start = pos + marker_len;
                end = start;
                while (end < end_trim
                       && omc_jumbf_is_digit((omc_u8)decoded[end])) {
                    end += 1U;
                }
                if (end > start
                    && (end == end_trim
                        || omc_jumbf_is_ref_token_boundary(decoded[end]))
                    && omc_jumbf_parse_decimal_u32(decoded, start, end,
                                                  out_index)) {
                    return 1;
                }
            }
            pos += 1U;
        }
    }
    return 0;
}

static int
omc_jumbf_ref_text_parse_label(const char* text, omc_u32 text_len,
                               char* out, omc_u32 out_cap,
                               omc_u32* out_len)
{
    char decoded[256];
    omc_u32 decoded_len;
    omc_u32 pos;
    omc_u32 end;
    int found_jumbf;

    if (text == (const char*)0 || out == (char*)0 || out_len == (omc_u32*)0
        || out_cap == 0U) {
        return 0;
    }
    if (!omc_jumbf_uri_percent_decode(text, text_len, decoded,
                                      (omc_u32)sizeof(decoded),
                                      &decoded_len)) {
        return 0;
    }

    pos = 0U;
    while (pos + 10U <= decoded_len) {
        if (omc_jumbf_ascii_icase_contains((const omc_u8*)(decoded + pos),
                                           decoded_len - pos, "c2pa.claim",
                                           10U)) {
            while (pos + 10U <= decoded_len
                   && memcmp(decoded + pos, "c2pa.claim", 10U) != 0) {
                pos += 1U;
            }
            break;
        }
        pos += 1U;
    }
    if (pos + 10U > decoded_len) {
        pos = 0U;
        found_jumbf = 0;
        while (pos + 6U <= decoded_len) {
            if (omc_jumbf_ascii_icase_contains((const omc_u8*)(decoded + pos),
                                               decoded_len - pos, "jumbf=",
                                               6U)) {
                while (pos + 6U <= decoded_len
                       && memcmp(decoded + pos, "jumbf=", 6U) != 0) {
                    pos += 1U;
                }
                pos += 6U;
                found_jumbf = 1;
                break;
            }
            pos += 1U;
        }
        if (!found_jumbf || pos >= decoded_len) {
            return 0;
        }
    }

    while (pos < decoded_len && (decoded[pos] == '#' || decoded[pos] == '/')) {
        pos += 1U;
    }
    end = pos;
    while (end < decoded_len
           && !omc_jumbf_is_label_token_boundary(decoded[end])) {
        end += 1U;
    }
    while (end > pos && (decoded[end - 1U] == '/'
                         || decoded[end - 1U] == '"'
                         || decoded[end - 1U] == '\'')) {
        end -= 1U;
    }
    if (end <= pos || end - pos + 1U > out_cap) {
        return 0;
    }
    memcpy(out, decoded + pos, end - pos);
    out[end - pos] = '\0';
    *out_len = end - pos;
    return 1;
}

static int
omc_jumbf_sig_proj_add_ref_index(omc_jumbf_sig_proj* sig, omc_u32 index)
{
    omc_u32 i;

    if (sig == (omc_jumbf_sig_proj*)0) {
        return 0;
    }
    for (i = 0U; i < sig->ref_index_count; ++i) {
        if (sig->ref_indices[i] == index) {
            return 1;
        }
    }
    if (sig->ref_index_count >= (omc_u32)(sizeof(sig->ref_indices)
                                          / sizeof(sig->ref_indices[0]))) {
        return 0;
    }
    sig->ref_indices[sig->ref_index_count++] = index;
    return 1;
}

static int
omc_jumbf_sig_proj_add_ref_label(omc_jumbf_sig_proj* sig, const char* label,
                                 omc_u32 label_len)
{
    omc_u32 i;

    if (sig == (omc_jumbf_sig_proj*)0 || label == (const char*)0
        || label_len == 0U) {
        return 0;
    }
    for (i = 0U; i < sig->ref_label_count; ++i) {
        if (sig->ref_label_lens[i] == label_len
            && memcmp(sig->ref_labels[i], label, label_len) == 0) {
            return 1;
        }
    }
    if (sig->ref_label_count >= (omc_u32)(sizeof(sig->ref_labels)
                                          / sizeof(sig->ref_labels[0]))
        || label_len + 1U > (omc_u32)sizeof(sig->ref_labels[0])) {
        return 0;
    }
    memcpy(sig->ref_labels[sig->ref_label_count], label, label_len);
    sig->ref_labels[sig->ref_label_count][label_len] = '\0';
    sig->ref_label_lens[sig->ref_label_count] = label_len;
    sig->ref_label_count += 1U;
    return 1;
}

static int
omc_jumbf_sig_proj_note_ref_scalar(omc_jumbf_sig_proj* sig,
                                   omc_const_bytes key, omc_elem_type elem_type,
                                   omc_u64 u64_value, omc_s64 i64_value)
{
    omc_u32 value;
    int have_value;

    if (sig == (omc_jumbf_sig_proj*)0) {
        return 0;
    }
    if (!(omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                      "claim_ref")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "claim_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "claim_ref_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "claim_reference_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference_index")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "ref_index")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "ref_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.claim_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.claim_ref")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.claim_ref_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.claim_reference_id")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.index")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references", "id")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references",
                 "claim_id")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references",
                 "claim_ref")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references",
                 "claim_ref_id")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references",
                 "claim_reference_id")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references",
                 "index"))) {
        return 1;
    }
    sig->reference_key_hits += 1U;

    value = 0U;
    have_value = 0;
    if (elem_type == OMC_ELEM_U64 || elem_type == OMC_ELEM_U32
        || elem_type == OMC_ELEM_U16 || elem_type == OMC_ELEM_U8) {
        if (u64_value <= 0xFFFFFFFFUL) {
            value = (omc_u32)u64_value;
            have_value = 1;
        }
    } else if (elem_type == OMC_ELEM_I64 || elem_type == OMC_ELEM_I32
               || elem_type == OMC_ELEM_I16 || elem_type == OMC_ELEM_I8) {
        if (i64_value >= 0 && (omc_u64)i64_value <= 0xFFFFFFFFUL) {
            value = (omc_u32)i64_value;
            have_value = 1;
        }
    }
    if (!have_value || !omc_jumbf_sig_proj_add_ref_index(sig, value)) {
        return 0;
    }
    if (!sig->has_claim_ref_index
        && omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                       "claim_ref")) {
        sig->claim_ref_index = value;
        sig->has_claim_ref_index = 1;
    }
    return 1;
}

static int
omc_jumbf_sig_proj_note_ref_text(omc_jumbf_sig_proj* sig,
                                 omc_const_bytes key, const char* text,
                                 omc_u32 text_len)
{
    omc_u32 ref_index;
    char label[128];
    omc_u32 label_len;

    if (sig == (omc_jumbf_sig_proj*)0 || text == (const char*)0) {
        return 0;
    }
    if (!(omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                      "claim_reference")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "claim_uri")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "jumbf_uri")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.claim_reference")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.uri")
          || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                         "reference.jumbf_uri")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references",
                 "claim_reference")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references", "uri")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references",
                 "jumbf_uri")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references", "href")
          || omc_jumbf_key_matches_indexed_field_suffix(
                 key, sig->prefix, sig->prefix_len, "references", "link"))) {
        return 1;
    }
    sig->reference_key_hits += 1U;

    if (omc_jumbf_ref_text_parse_index(text, text_len, &ref_index)
        && !omc_jumbf_sig_proj_add_ref_index(sig, ref_index)) {
        return 0;
    }
    if (omc_jumbf_ref_text_parse_label(text, text_len, label,
                                       (omc_u32)sizeof(label), &label_len)
        && !omc_jumbf_sig_proj_add_ref_label(sig, label, label_len)) {
        return 0;
    }
    return 1;
}

static int
omc_jumbf_parse_jumd_label(const omc_u8* payload, omc_u64 payload_size,
                           char* out, omc_u32 out_cap, omc_u32* out_len)
{
    omc_u64 start;
    omc_u64 end;
    omc_u64 i;

    if (payload == (const omc_u8*)0 || out == (char*)0
        || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }
    start = 0U;
    if (payload_size >= 21U) {
        start = 20U;
    }
    if (start >= payload_size) {
        return 0;
    }
    end = start;
    while (end < payload_size && payload[end] != 0U) {
        end += 1U;
    }
    if (end <= start || end - start + 1U > out_cap || end - start > 256U) {
        return 0;
    }
    for (i = start; i < end; ++i) {
        if (!omc_jumbf_is_printable_ascii(payload[i])) {
            return 0;
        }
    }
    memcpy(out, payload + start, (omc_size)(end - start));
    out[end - start] = '\0';
    *out_len = (omc_u32)(end - start);
    return 1;
}

static int
omc_jumbf_extract_cbor_root_prefix(omc_const_bytes key, char* out,
                                   omc_u32 out_cap, omc_u32* out_len)
{
    omc_u32 pos;

    if (key.data == (const omc_u8*)0 || out == (char*)0
        || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }
    pos = 0U;
    while (pos + 5U <= key.size) {
        if (memcmp(key.data + pos, ".cbor", 5U) == 0) {
            omc_u32 len;

            len = pos + 5U;
            if (len + 1U > out_cap) {
                return 0;
            }
            memcpy(out, key.data, len);
            out[len] = '\0';
            *out_len = len;
            return 1;
        }
        pos += 1U;
    }
    return 0;
}

static int
omc_jumbf_find_claim_prefix_by_label(omc_jumbf_ctx* ctx, omc_size scan_limit,
                                     const char* label, omc_u32 label_len,
                                     char* out, omc_u32 out_cap,
                                     omc_u32* out_len, omc_u32* out_matches)
{
    omc_size i;
    omc_u32 match_state;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || label == (const char*)0 || out == (char*)0
        || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }
    (void)label_len;
    match_state = 0U;
    out[0] = '\0';
    *out_len = 0U;

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;
        omc_const_bytes label_view;
        char box_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 box_prefix_len;
        omc_size j;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_FIELD
            || entry->value.kind != OMC_VAL_TEXT) {
            continue;
        }
        field_view = omc_jumbf_entry_key_view(ctx->store, entry);
        label_view = omc_jumbf_entry_value_view(ctx->store, entry);
        if (field_view.size < 6U
            || memcmp(field_view.data + field_view.size - 6U, ".label", 6U)
                   != 0
            || !omc_jumbf_ascii_icase_contains(label_view.data, label_view.size,
                                               "claim", 256U)
            || !omc_jumbf_ascii_icase_contains(label_view.data, label_view.size,
                                               label, 256U)) {
            continue;
        }

        if (field_view.size - 6U + 1U > (omc_u32)sizeof(box_prefix)) {
            return 0;
        }
        memcpy(box_prefix, field_view.data, field_view.size - 6U);
        box_prefix[field_view.size - 6U] = '\0';
        box_prefix_len = (omc_u32)(field_view.size - 6U);

        for (j = 0U; j < scan_limit && j < ctx->store->entry_count; ++j) {
            const omc_entry* cbor_entry;
            omc_const_bytes key_view;
            char cbor_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 cbor_prefix_len;
            char resolved_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 resolved_prefix_len;
            omc_u32 resolved_matches;
            const omc_u8* claim_bytes;
            omc_u64 claim_size;

            cbor_entry = &ctx->store->entries[j];
            if (cbor_entry->origin.block != ctx->block
                || cbor_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                continue;
            }
            key_view = omc_jumbf_entry_key_view(ctx->store, cbor_entry);
            if (key_view.size <= box_prefix_len
                || memcmp(key_view.data, box_prefix, box_prefix_len) != 0
                || key_view.data[box_prefix_len] != (omc_u8)'.'
                || !omc_jumbf_extract_cbor_root_prefix(
                       key_view, cbor_prefix, (omc_u32)sizeof(cbor_prefix),
                       &cbor_prefix_len)) {
                continue;
            }
            resolved_matches = 0U;
            if (omc_jumbf_extract_box_payload_for_cbor_prefix(
                    ctx, scan_limit, cbor_prefix, cbor_prefix_len,
                    &claim_bytes, &claim_size)
                && omc_jumbf_find_semantic_claim_prefix_by_bytes(
                       ctx, scan_limit, claim_bytes, claim_size,
                       resolved_prefix, (omc_u32)sizeof(resolved_prefix),
                       &resolved_prefix_len, &resolved_matches)) {
                cbor_prefix_len = resolved_prefix_len;
                memcpy(cbor_prefix, resolved_prefix, cbor_prefix_len + 1U);
            } else {
                resolved_matches = 1U;
            }
            if (match_state == 0U) {
                if (cbor_prefix_len + 1U > out_cap) {
                    return 0;
                }
                memcpy(out, cbor_prefix, cbor_prefix_len + 1U);
                *out_len = cbor_prefix_len;
                match_state = 1U;
            } else if (*out_len != cbor_prefix_len
                       || memcmp(out, cbor_prefix, cbor_prefix_len) != 0) {
                match_state = 2U;
            }
            if (resolved_matches > 1U) {
                match_state = 2U;
            }
            break;
        }
    }
    if (out_matches != (omc_u32*)0) {
        *out_matches = match_state;
    }
    return match_state != 0U;
}

static int
omc_jumbf_find_claim_payload_ref(omc_jumbf_ctx* ctx, omc_size scan_limit,
                                 const char* claim_prefix,
                                 omc_u32 claim_prefix_len,
                                 omc_byte_ref* out_ref)
{
    omc_size i;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || claim_prefix == (const char*)0 || out_ref == (omc_byte_ref*)0) {
        return 0;
    }

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY
            || (entry->value.kind != OMC_VAL_BYTES
                && entry->value.kind != OMC_VAL_TEXT)) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (key.size != claim_prefix_len + 6U
            || memcmp(key.data, claim_prefix, claim_prefix_len) != 0
            || key.data[claim_prefix_len] != (omc_u8)'.'
            || memcmp(key.data + claim_prefix_len + 1U, "claim", 5U) != 0) {
            continue;
        }
        *out_ref = entry->value.u.ref;
        return 1;
    }
    return 0;
}

static int
omc_jumbf_detached_payload_add_ref(omc_byte_ref* refs, omc_u32 cap,
                                   omc_u32* io_count, omc_byte_ref ref)
{
    omc_u32 i;

    if (refs == (omc_byte_ref*)0 || io_count == (omc_u32*)0 || cap == 0U) {
        return 0;
    }
    for (i = 0U; i < *io_count; ++i) {
        if (refs[i].offset == ref.offset && refs[i].size == ref.size) {
            return 1;
        }
    }
    if (*io_count >= cap) {
        return 0;
    }
    refs[*io_count] = ref;
    *io_count += 1U;
    return 1;
}

static int
omc_jumbf_collect_detached_payload_refs(omc_jumbf_ctx* ctx,
                                        omc_size scan_limit,
                                        const omc_jumbf_sig_proj* sig,
                                        omc_byte_ref* refs, omc_u32 cap,
                                        omc_u32* out_count)
{
    omc_u32 count;
    int explicit_refs;
    int ambiguous;

    if (ctx == (omc_jumbf_ctx*)0 || sig == (const omc_jumbf_sig_proj*)0
        || refs == (omc_byte_ref*)0 || out_count == (omc_u32*)0
        || cap == 0U) {
        return 0;
    }

    count = 0U;
    explicit_refs = (sig->ref_index_count != 0U || sig->ref_label_count != 0U);
    ambiguous = 0;

    if (explicit_refs) {
        omc_u32 i;

        for (i = 0U; i < sig->ref_index_count; ++i) {
            char claim_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 claim_prefix_len;
            omc_byte_ref payload_ref;

            if (!omc_jumbf_find_claim_prefix_by_semantic_index(
                    ctx, scan_limit, sig->ref_indices[i], claim_prefix,
                    (omc_u32)sizeof(claim_prefix), &claim_prefix_len)) {
                continue;
            }
            if (!omc_jumbf_find_claim_payload_ref(ctx, scan_limit, claim_prefix,
                                                  claim_prefix_len,
                                                  &payload_ref)
                || !omc_jumbf_detached_payload_add_ref(refs, cap, &count,
                                                       payload_ref)) {
                return 0;
            }
        }

        for (i = 0U; i < sig->ref_label_count; ++i) {
            char claim_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 claim_prefix_len;
            omc_u32 claim_matches;
            omc_byte_ref payload_ref;

            claim_matches = 0U;
            if (!omc_jumbf_find_claim_prefix_by_label(
                    ctx, scan_limit, sig->ref_labels[i], sig->ref_label_lens[i],
                    claim_prefix, (omc_u32)sizeof(claim_prefix),
                    &claim_prefix_len, &claim_matches)) {
                continue;
            }
            if (claim_matches > 1U) {
                ambiguous = 1;
                continue;
            }
            if (!omc_jumbf_find_claim_payload_ref(ctx, scan_limit, claim_prefix,
                                                  claim_prefix_len,
                                                  &payload_ref)
                || !omc_jumbf_detached_payload_add_ref(refs, cap, &count,
                                                       payload_ref)) {
                return 0;
            }
        }

        *out_count = count;
        return !ambiguous && count == 1U;
    }

    if (sig->has_direct_claim_prefix) {
        omc_byte_ref payload_ref;

        if (omc_jumbf_find_claim_payload_ref(ctx, scan_limit,
                                             sig->direct_claim_prefix,
                                             sig->direct_claim_prefix_len,
                                             &payload_ref)
            && !omc_jumbf_detached_payload_add_ref(refs, cap, &count,
                                                   payload_ref)) {
            return 0;
        }
    }

    *out_count = count;
    return count == 1U;
}

static int
omc_jumbf_sig_link_has_target(const omc_jumbf_sig_link_proj* sig_link,
                              const char* claim_prefix,
                              omc_u32 claim_prefix_len, int* out_direct,
                              int* out_cross)
{
    omc_u32 i;

    if (sig_link == (const omc_jumbf_sig_link_proj*)0
        || claim_prefix == (const char*)0) {
        return 0;
    }
    for (i = 0U; i < sig_link->target_claim_count; ++i) {
        if (sig_link->target_claim_lens[i] == claim_prefix_len
            && memcmp(sig_link->target_claim_prefixes[i], claim_prefix,
                      claim_prefix_len)
                   == 0) {
            int is_direct;

            is_direct = sig_link->has_source_claim_prefix
                        && sig_link->source_claim_prefix_len == claim_prefix_len
                        && memcmp(sig_link->source_claim_prefix, claim_prefix,
                                  claim_prefix_len)
                               == 0;
            if (out_direct != (int*)0) {
                *out_direct = is_direct;
            }
            if (out_cross != (int*)0) {
                *out_cross = !is_direct;
            }
            return 1;
        }
    }
    return 0;
}

static void
omc_jumbf_claim_link_stats_init(omc_jumbf_claim_link_stats* stats)
{
    if (stats == (omc_jumbf_claim_link_stats*)0) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

static int
omc_jumbf_prefix_eq_text(const char* lhs, omc_u32 lhs_len, const char* rhs,
                         omc_u32 rhs_len)
{
    return lhs != (const char*)0 && rhs != (const char*)0
           && lhs_len == rhs_len && memcmp(lhs, rhs, lhs_len) == 0;
}

static void
omc_jumbf_sig_link_proj_init(omc_jumbf_sig_link_proj* proj)
{
    if (proj == (omc_jumbf_sig_link_proj*)0) {
        return;
    }
    memset(proj, 0, sizeof(*proj));
}

static int
omc_jumbf_sig_link_add_target(omc_jumbf_sig_link_proj* link,
                              const char* claim_prefix,
                              omc_u32 claim_prefix_len)
{
    omc_u32 i;

    if (link == (omc_jumbf_sig_link_proj*)0 || claim_prefix == (const char*)0
        || claim_prefix_len == 0U) {
        return 0;
    }
    for (i = 0U; i < link->target_claim_count; ++i) {
        if (link->target_claim_lens[i] == claim_prefix_len
            && memcmp(link->target_claim_prefixes[i], claim_prefix,
                      claim_prefix_len)
                   == 0) {
            return 1;
        }
    }
    if (link->target_claim_count
        >= (omc_u32)(sizeof(link->target_claim_lens)
                     / sizeof(link->target_claim_lens[0]))
        || claim_prefix_len + 1U
               > (omc_u32)sizeof(link->target_claim_prefixes[0])) {
        return 0;
    }
    memcpy(link->target_claim_prefixes[link->target_claim_count], claim_prefix,
           claim_prefix_len);
    link->target_claim_prefixes[link->target_claim_count][claim_prefix_len]
        = '\0';
    link->target_claim_lens[link->target_claim_count] = claim_prefix_len;
    link->target_claim_count += 1U;
    return 1;
}

static int
omc_jumbf_sig_link_note_target(omc_jumbf_ctx* ctx, omc_size scan_limit,
                               const omc_jumbf_sig_proj* sig,
                               omc_jumbf_sig_link_proj* out_link,
                               const char* claim_prefix,
                               omc_u32 claim_prefix_len)
{
    omc_jumbf_ing_summary summary;
    int is_direct;

    if (ctx == (omc_jumbf_ctx*)0 || sig == (const omc_jumbf_sig_proj*)0
        || out_link == (omc_jumbf_sig_link_proj*)0
        || claim_prefix == (const char*)0 || claim_prefix_len == 0U) {
        return 0;
    }
    if (!omc_jumbf_sig_link_add_target(out_link, claim_prefix,
                                       claim_prefix_len)) {
        return 0;
    }
    is_direct = sig->has_direct_claim_prefix
                && omc_jumbf_prefix_eq_text(sig->direct_claim_prefix,
                                            sig->direct_claim_prefix_len,
                                            claim_prefix, claim_prefix_len);
    if (is_direct) {
        out_link->target_direct_claim_count += 1U;
    } else {
        out_link->target_cross_claim_count += 1U;
    }
    if (!omc_jumbf_collect_claim_ingredient_summary(ctx, scan_limit,
                                                    claim_prefix,
                                                    claim_prefix_len,
                                                    &summary)) {
        return 0;
    }
    out_link->explicit_reference_resolved_claim_count
        = (omc_u64)out_link->target_claim_count;
    if (summary.ingredient_count != 0U) {
        out_link->linked_claim_count += 1U;
        omc_jumbf_ing_summary_merge(&out_link->ingredient_summary, &summary);
        if (is_direct) {
            out_link->linked_direct_claim_count += 1U;
            omc_jumbf_ing_summary_merge(&out_link->direct_summary, &summary);
        } else {
            out_link->linked_cross_claim_count += 1U;
            omc_jumbf_ing_summary_merge(&out_link->cross_summary, &summary);
        }
    }
    if (!out_link->has_target_claim) {
        out_link->has_target_claim = 1;
        if (claim_prefix_len + 1U
            <= (omc_u32)sizeof(out_link->target_claim_prefix)) {
            memcpy(out_link->target_claim_prefix, claim_prefix,
                   claim_prefix_len);
            out_link->target_claim_prefix[claim_prefix_len] = '\0';
            out_link->target_claim_prefix_len = claim_prefix_len;
            if (omc_jumbf_extract_manifest_prefix_from_claim_prefix(
                    claim_prefix, claim_prefix_len,
                    out_link->target_manifest_prefix,
                    (omc_u32)sizeof(out_link->target_manifest_prefix),
                    &out_link->target_manifest_prefix_len)) {
                out_link->has_target_manifest = 1;
            }
        }
    }
    return 1;
}

static int
omc_jumbf_collect_signature_ingredient_link(omc_jumbf_ctx* ctx,
                                            omc_size scan_limit,
                                            const omc_jumbf_sig_proj* sig,
                                            omc_jumbf_sig_link_proj* out_link)
{
    if (ctx == (omc_jumbf_ctx*)0 || sig == (const omc_jumbf_sig_proj*)0
        || out_link == (omc_jumbf_sig_link_proj*)0) {
        return 0;
    }

    omc_jumbf_sig_link_proj_init(out_link);
    if (sig->has_direct_claim_prefix) {
        omc_jumbf_ing_summary source_summary;

        memcpy(out_link->source_claim_prefix, sig->direct_claim_prefix,
               sig->direct_claim_prefix_len);
        out_link->source_claim_prefix[sig->direct_claim_prefix_len] = '\0';
        out_link->source_claim_prefix_len = sig->direct_claim_prefix_len;
        out_link->has_source_claim_prefix = 1;
        if (!omc_jumbf_collect_claim_ingredient_summary(
                ctx, scan_limit, sig->direct_claim_prefix,
                sig->direct_claim_prefix_len, &source_summary)) {
            return 0;
        }
        out_link->direct_claim_has_ingredients
            = (source_summary.ingredient_count != 0U);
    }
    out_link->explicit_reference_present
        = (sig->ref_index_count != 0U || sig->ref_label_count != 0U);
    out_link->explicit_reference_index_hits = (omc_u64)sig->ref_index_count;
    out_link->explicit_reference_label_hits = (omc_u64)sig->ref_label_count;

    if (!out_link->explicit_reference_present) {
        if (sig->has_direct_claim_prefix
            && !omc_jumbf_sig_link_note_target(
                   ctx, scan_limit, sig, out_link, sig->direct_claim_prefix,
                   sig->direct_claim_prefix_len)) {
            return 0;
        }
        out_link->explicit_reference_resolved_claim_count = 0U;
    } else {
        omc_u32 i;
        for (i = 0U; i < sig->ref_index_count; ++i) {
            char claim_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 claim_prefix_len;

            if (omc_jumbf_find_claim_prefix_by_semantic_index(
                    ctx, scan_limit, sig->ref_indices[i], claim_prefix,
                    (omc_u32)sizeof(claim_prefix), &claim_prefix_len)) {
                if (!omc_jumbf_sig_link_note_target(ctx, scan_limit, sig,
                                                    out_link, claim_prefix,
                                                    claim_prefix_len)) {
                    return 0;
                }
            }
        }
        for (i = 0U; i < sig->ref_label_count; ++i) {
            char claim_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 claim_prefix_len;
            omc_u32 claim_matches;

            claim_matches = 0U;
            if (omc_jumbf_find_claim_prefix_by_label(
                    ctx, scan_limit, sig->ref_labels[i], sig->ref_label_lens[i],
                    claim_prefix, (omc_u32)sizeof(claim_prefix),
                    &claim_prefix_len, &claim_matches)) {
                if (!omc_jumbf_sig_link_note_target(ctx, scan_limit, sig,
                                                    out_link, claim_prefix,
                                                    claim_prefix_len)) {
                    return 0;
                }
                if (claim_matches > 1U) {
                    out_link->explicit_reference_ambiguous = 1;
                }
            }
        }

        if (out_link->target_claim_count == 0U) {
            out_link->explicit_reference_unresolved = 1;
        } else if (out_link->target_claim_count > 1U
                   || out_link->explicit_reference_ambiguous) {
            out_link->explicit_reference_ambiguous = 1;
        }
    }

    out_link->direct_source = (out_link->linked_direct_claim_count != 0U);
    out_link->cross_source = (out_link->linked_cross_claim_count != 0U);
    if (out_link->linked_claim_count != 0U && !out_link->direct_source
        && !out_link->cross_source) {
        out_link->cross_source = 1;
    }
    return 1;
}

static int
omc_jumbf_collect_claim_signature_topology(
    omc_jumbf_ctx* ctx, omc_size scan_limit, const char* claim_prefix,
    omc_u32 claim_prefix_len, omc_jumbf_claim_link_stats* out_stats)
{
    omc_size i;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || claim_prefix == (const char*)0
        || out_stats == (omc_jumbf_claim_link_stats*)0) {
        return 0;
    }

    omc_jumbf_claim_link_stats_init(out_stats);

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char sig_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 sig_prefix_len;
        omc_jumbf_sig_proj sig;
        omc_jumbf_sig_link_proj sig_link;
        int direct_match;
        int cross_match;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (!omc_jumbf_find_indexed_segment_prefix(key, ".signatures[",
                                                   sig_prefix,
                                                   (omc_u32)sizeof(sig_prefix),
                                                   &sig_prefix_len)
            || omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                            i, ".signatures[", sig_prefix,
                                            sig_prefix_len)) {
            continue;
        }
        if (!omc_jumbf_collect_signature_projection(
                ctx, scan_limit, sig_prefix, sig_prefix_len, &sig)
            || !omc_jumbf_collect_signature_ingredient_link(
                ctx, scan_limit, &sig, &sig_link)) {
            if (ctx->res.status == OMC_JUMBF_OK) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
            }
            return 0;
        }

        if (sig.has_direct_claim_prefix
            && omc_jumbf_prefix_eq_text(sig.direct_claim_prefix,
                                        sig.direct_claim_prefix_len,
                                        claim_prefix, claim_prefix_len)) {
            out_stats->nested_signature_count += 1U;
        }
        direct_match = 0;
        cross_match = 0;
        if (!omc_jumbf_sig_link_has_target(&sig_link, claim_prefix,
                                           claim_prefix_len, &direct_match,
                                           &cross_match)) {
            continue;
        }
        if (cross_match) {
            out_stats->referenced_by_signature_count += 1U;
        }
        if (sig_link.ingredient_summary.ingredient_count == 0U
            && !sig_link.explicit_reference_ambiguous) {
            continue;
        }

        out_stats->linked_signature_count += 1U;
        omc_jumbf_ing_summary_merge(&out_stats->linked_summary,
                                    &sig_link.ingredient_summary);
        if (direct_match) {
            out_stats->linked_direct_signature_count += 1U;
            omc_jumbf_ing_summary_merge(&out_stats->linked_direct_summary,
                                        &sig_link.ingredient_summary);
        }
        if (cross_match) {
            out_stats->linked_cross_signature_count += 1U;
            omc_jumbf_ing_summary_merge(&out_stats->linked_cross_summary,
                                        &sig_link.ingredient_summary);
        }
        if (sig_link.explicit_reference_present) {
            out_stats->linked_explicit_reference_signature_count += 1U;
            omc_jumbf_ing_summary_merge(
                &out_stats->linked_explicit_reference_summary,
                &sig_link.ingredient_summary);
            if (direct_match) {
                out_stats->linked_explicit_reference_direct_signature_count
                    += 1U;
                omc_jumbf_ing_summary_merge(
                    &out_stats->linked_explicit_reference_direct_summary,
                    &sig_link.ingredient_summary);
            }
            if (cross_match) {
                out_stats->linked_explicit_reference_cross_signature_count
                    += 1U;
                omc_jumbf_ing_summary_merge(
                    &out_stats->linked_explicit_reference_cross_summary,
                    &sig_link.ingredient_summary);
            }
            if (sig_link.explicit_reference_unresolved) {
                out_stats
                    ->linked_explicit_reference_unresolved_signature_count += 1U;
                omc_jumbf_ing_summary_merge(
                    &out_stats->linked_explicit_reference_unresolved_summary,
                    &sig_link.ingredient_summary);
                if (direct_match) {
                    out_stats
                        ->linked_explicit_reference_unresolved_direct_signature_count
                        += 1U;
                    omc_jumbf_ing_summary_merge(
                        &out_stats
                             ->linked_explicit_reference_unresolved_direct_summary,
                        &sig_link.ingredient_summary);
                }
                if (cross_match) {
                    out_stats
                        ->linked_explicit_reference_unresolved_cross_signature_count
                        += 1U;
                    omc_jumbf_ing_summary_merge(
                        &out_stats
                             ->linked_explicit_reference_unresolved_cross_summary,
                        &sig_link.ingredient_summary);
                }
            }
            if (sig_link.explicit_reference_ambiguous) {
                out_stats->linked_explicit_reference_ambiguous_signature_count
                    += 1U;
                omc_jumbf_ing_summary_merge(
                    &out_stats->linked_explicit_reference_ambiguous_summary,
                    &sig_link.ingredient_summary);
                if (direct_match) {
                    out_stats
                        ->linked_explicit_reference_ambiguous_direct_signature_count
                        += 1U;
                    omc_jumbf_ing_summary_merge(
                        &out_stats
                             ->linked_explicit_reference_ambiguous_direct_summary,
                        &sig_link.ingredient_summary);
                }
                if (cross_match) {
                    out_stats
                        ->linked_explicit_reference_ambiguous_cross_signature_count
                        += 1U;
                    omc_jumbf_ing_summary_merge(
                        &out_stats
                             ->linked_explicit_reference_ambiguous_cross_summary,
                        &sig_link.ingredient_summary);
                }
            }
        }
    }
    return 1;
}

static void
omc_jumbf_claim_signature_stats_init(omc_jumbf_claim_signature_stats* stats)
{
    if (stats == (omc_jumbf_claim_signature_stats*)0) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

static int
omc_jumbf_collect_claim_signature_stats(
    omc_jumbf_ctx* ctx, omc_size scan_limit, const char* claim_prefix,
    omc_u32 claim_prefix_len, omc_jumbf_claim_signature_stats* out)
{
    omc_size i;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || claim_prefix == (const char*)0
        || out == (omc_jumbf_claim_signature_stats*)0) {
        return 0;
    }

    omc_jumbf_claim_signature_stats_init(out);
    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char sig_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 sig_prefix_len;
        omc_jumbf_sig_proj sig;
        omc_jumbf_sig_link_proj sig_link;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (!omc_jumbf_find_indexed_segment_prefix(key, ".signatures[",
                                                   sig_prefix,
                                                   (omc_u32)sizeof(sig_prefix),
                                                   &sig_prefix_len)
            || !omc_jumbf_prefix_is_nested_under(sig_prefix, sig_prefix_len,
                                                 claim_prefix,
                                                 claim_prefix_len)
            || omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                            i, ".signatures[", sig_prefix,
                                            sig_prefix_len)) {
            continue;
        }
        if (!omc_jumbf_collect_signature_projection(
                ctx, scan_limit, sig_prefix, sig_prefix_len, &sig)
            || !omc_jumbf_collect_signature_ingredient_link(
                   ctx, scan_limit, &sig, &sig_link)) {
            if (ctx->res.status == OMC_JUMBF_OK) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
            }
            return 0;
        }

        out->signature_count += 1U;
        if (sig_link.target_claim_count != 0U) {
            out->signature_linked_count += 1U;
        }
        if (sig_link.explicit_reference_present) {
            out->explicit_reference_signature_count += 1U;
            out->explicit_reference_index_hits
                += sig_link.explicit_reference_index_hits;
            out->explicit_reference_label_hits
                += sig_link.explicit_reference_label_hits;
            if (sig_link.explicit_reference_unresolved) {
                out->explicit_reference_unresolved_signature_count += 1U;
            } else if (sig_link.explicit_reference_ambiguous) {
                out->explicit_reference_ambiguous_signature_count += 1U;
            }
        }
    }
    return 1;
}

static const char*
omc_jumbf_cbor_major_suffix(omc_u8 major)
{
    switch (major) {
        case 0U: return "u";
        case 1U: return "n";
        case 2U: return "bytes";
        case 3U: return "text";
        case 4U: return "arr";
        case 5U: return "map";
        case 6U: return "tag";
        case 7U: return "simple";
        default: return "key";
    }
}

static int
omc_jumbf_make_synth_key_segment(omc_u32 map_index, const char* suffix,
                                 char* out, omc_u32 out_cap,
                                 omc_u32* out_len)
{
    omc_u32 len;
    omc_u32 suffix_len;

    len = 0U;
    suffix_len = omc_jumbf_cstr_size(suffix);
    if (out == (char*)0 || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }
    out[0] = '\0';

    if (!omc_jumbf_append_char(out, out_cap, &len, 'k')
        || !omc_jumbf_append_u32_dec(out, out_cap, &len, map_index)
        || !omc_jumbf_append_char(out, out_cap, &len, '_')
        || !omc_jumbf_append_mem(out, out_cap, &len, suffix, suffix_len)) {
        return 0;
    }

    *out_len = len;
    return 1;
}

static omc_u32
omc_jumbf_cbor_half_to_f32_bits(omc_u16 half_bits)
{
    omc_u32 sign;
    omc_u32 exp;
    omc_u32 frac;

    sign = ((omc_u32)(half_bits & 0x8000U)) << 16U;
    exp = (omc_u32)((half_bits >> 10U) & 0x1FU);
    frac = (omc_u32)(half_bits & 0x03FFU);

    if (exp == 0U) {
        int shift;

        if (frac == 0U) {
            return sign;
        }

        shift = 0;
        while ((frac & 0x0400U) == 0U) {
            frac <<= 1U;
            shift += 1;
        }
        frac &= 0x03FFU;
        exp = (omc_u32)(127 - 15 - shift + 1);
        return sign | (exp << 23U) | (frac << 13U);
    }
    if (exp == 31U) {
        return sign | 0x7F800000U | (frac << 13U);
    }

    exp = exp + (omc_u32)(127 - 15);
    return sign | (exp << 23U) | (frac << 13U);
}

static int
omc_jumbf_cbor_peek_break(const omc_u8* bytes, omc_size size, omc_u64 pos)
{
    return pos < (omc_u64)size && bytes[(omc_size)pos] == 0xFFU;
}

static int
omc_jumbf_cbor_consume_break(const omc_u8* bytes, omc_size size,
                             omc_u64* pos)
{
    if (pos == (omc_u64*)0
        || !omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
        return 0;
    }
    *pos += 1U;
    return 1;
}

static int
omc_jumbf_cbor_read_head_raw(const omc_u8* bytes, omc_size size, omc_u64* pos,
                             omc_jumbf_cbor_head* out_head)
{
    omc_u8 lead;
    omc_u8 addl;
    omc_u64 arg;

    if (bytes == (const omc_u8*)0 || pos == (omc_u64*)0
        || out_head == (omc_jumbf_cbor_head*)0) {
        return 0;
    }
    if (*pos >= (omc_u64)size) {
        return 0;
    }

    lead = bytes[(omc_size)*pos];
    *pos += 1U;

    addl = (omc_u8)(lead & 0x1FU);
    out_head->major = (omc_u8)(lead >> 5);
    out_head->addl = addl;
    out_head->indefinite = 0;
    out_head->arg = 0U;

    if (addl < 24U) {
        out_head->arg = addl;
        return 1;
    }
    if (addl == 24U) {
        if (*pos >= (omc_u64)size) {
            return 0;
        }
        out_head->arg = bytes[(omc_size)*pos];
        *pos += 1U;
        return 1;
    }
    if (addl == 25U) {
        omc_u32 v;

        if (*pos > (omc_u64)size || ((omc_u64)size - *pos) < 2U) {
            return 0;
        }
        v = (((omc_u32)bytes[(omc_size)*pos + 0U]) << 8)
            | (((omc_u32)bytes[(omc_size)*pos + 1U]) << 0);
        *pos += 2U;
        out_head->arg = v;
        return 1;
    }
    if (addl == 26U) {
        omc_u32 v32;

        if (!omc_jumbf_read_u32be(bytes, size, *pos, &v32)) {
            return 0;
        }
        *pos += 4U;
        out_head->arg = v32;
        return 1;
    }
    if (addl == 27U) {
        if (!omc_jumbf_read_u64be(bytes, size, *pos, &arg)) {
            return 0;
        }
        *pos += 8U;
        out_head->arg = arg;
        return 1;
    }
    if (addl == 31U) {
        if (out_head->major == 7U) {
            return 0;
        }
        out_head->indefinite = 1;
        return 1;
    }
    return 0;
}

static int
omc_jumbf_cbor_read_head(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                         omc_size size, omc_u64* pos,
                         omc_jumbf_cbor_head* out_head)
{
    if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, out_head)
        || !omc_jumbf_take_cbor_item(ctx)) {
        if (ctx != (omc_jumbf_ctx*)0 && ctx->res.status == OMC_JUMBF_OK) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
        }
        return 0;
    }
    return 1;
}

static int
omc_jumbf_cbor_depth_ok(omc_jumbf_ctx* ctx, omc_u32 depth)
{
    if (ctx == (omc_jumbf_ctx*)0) {
        return 0;
    }
    if (ctx->opts.limits.max_cbor_depth != 0U
        && depth > ctx->opts.limits.max_cbor_depth) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return 1;
}

static int
omc_jumbf_cbor_read_definite_payload(const omc_u8* bytes, omc_size size,
                                     omc_u64* pos,
                                     const omc_jumbf_cbor_head* head,
                                     omc_u64 limit, const omc_u8** out_bytes,
                                     omc_u32* out_size)
{
    if (pos == (omc_u64*)0 || head == (const omc_jumbf_cbor_head*)0
        || out_bytes == (const omc_u8**)0 || out_size == (omc_u32*)0) {
        return 0;
    }
    if (head->indefinite || head->arg > limit || head->arg > (omc_u64)(~0U)
        || *pos > (omc_u64)size || head->arg > ((omc_u64)size - *pos)) {
        return 0;
    }

    *out_bytes = bytes + (omc_size)*pos;
    *out_size = (omc_u32)head->arg;
    *pos += head->arg;
    return 1;
}

static int omc_jumbf_cbor_skip_item(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                    omc_size size, omc_u64* pos,
                                    omc_u32 depth);
static int omc_jumbf_cbor_sanitize_segment(const omc_u8* bytes, omc_u32 size,
                                           char* out, omc_u32 out_cap,
                                           omc_u32* out_len);

static int
omc_jumbf_cbor_read_key_text(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                             omc_size size, omc_u64* pos,
                             const omc_jumbf_cbor_head* head,
                             omc_u64 max_key, char* out_seg,
                             omc_u32 out_cap, omc_u32* out_len)
{
    omc_u8 raw[OMC_JUMBF_PATH_CAP];
    omc_u64 total;

    if (ctx == (omc_jumbf_ctx*)0 || pos == (omc_u64*)0
        || head == (const omc_jumbf_cbor_head*)0 || out_seg == (char*)0
        || out_len == (omc_u32*)0 || head->major != 3U) {
        return 0;
    }

    total = 0U;
    if (!head->indefinite) {
        const omc_u8* payload;
        omc_u32 payload_size;

        if (!omc_jumbf_cbor_read_definite_payload(bytes, size, pos, head,
                                                  max_key, &payload,
                                                  &payload_size)
            || !omc_jumbf_cbor_sanitize_segment(payload, payload_size, out_seg,
                                                out_cap, out_len)) {
            ctx->res.status = (head->arg > max_key) ? OMC_JUMBF_LIMIT
                                                    : OMC_JUMBF_MALFORMED;
            return 0;
        }
        return 1;
    }

    while (1) {
        omc_jumbf_cbor_head chunk;

        if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
            if (!omc_jumbf_cbor_consume_break(bytes, size, pos)
                || !omc_jumbf_cbor_sanitize_segment(raw, (omc_u32)total,
                                                    out_seg, out_cap,
                                                    out_len)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            return 1;
        }
        if (!omc_jumbf_cbor_read_head(ctx, bytes, size, pos, &chunk)) {
            return 0;
        }
        if (chunk.major != 3U || chunk.indefinite) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }
        if (*pos > (omc_u64)size || chunk.arg > ((omc_u64)size - *pos)) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }
        if (total > ((omc_u64)(~0U) - chunk.arg)
            || total + chunk.arg > max_key
            || total + chunk.arg > (omc_u64)sizeof(raw)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }

        memcpy(raw + (omc_u32)total, bytes + (omc_size)*pos,
               (omc_size)chunk.arg);
        total += chunk.arg;
        *pos += chunk.arg;
    }
}

static int
omc_jumbf_cbor_collect_byte_text_ref(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                     omc_size size, omc_u64* pos,
                                     const omc_jumbf_cbor_head* head,
                                     omc_u64 limit, omc_byte_ref* out_ref,
                                     omc_u32* out_size)
{
    omc_u64 total;
    int have_ref;

    if (ctx == (omc_jumbf_ctx*)0 || pos == (omc_u64*)0
        || head == (const omc_jumbf_cbor_head*)0 || out_ref == (omc_byte_ref*)0
        || out_size == (omc_u32*)0) {
        return 0;
    }
    if (head->major != 2U && head->major != 3U) {
        return 0;
    }

    out_ref->offset = 0U;
    out_ref->size = 0U;
    *out_size = 0U;
    total = 0U;
    have_ref = 0;

    if (!head->indefinite) {
        const omc_u8* payload;

        if (!omc_jumbf_cbor_read_definite_payload(bytes, size, pos, head,
                                                  limit, &payload, out_size)) {
            ctx->res.status = (head->arg > limit || head->arg > (omc_u64)(~0U))
                                  ? OMC_JUMBF_LIMIT
                                  : OMC_JUMBF_MALFORMED;
            return 0;
        }
        if (ctx->store != (omc_store*)0
            && !omc_jumbf_store_bytes(ctx, payload, *out_size, out_ref)) {
            return 0;
        } else if (ctx->store == (omc_store*)0) {
            out_ref->size = *out_size;
        }
        return 1;
    }

    while (1) {
        omc_jumbf_cbor_head chunk;

        if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
            if (!omc_jumbf_cbor_consume_break(bytes, size, pos)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            if (!have_ref && ctx->store != (omc_store*)0) {
                out_ref->offset = (omc_u32)ctx->store->arena.size;
                out_ref->size = 0U;
            } else if (!have_ref) {
                out_ref->size = 0U;
            }
            *out_size = (omc_u32)total;
            return 1;
        }
        if (!omc_jumbf_cbor_read_head(ctx, bytes, size, pos, &chunk)) {
            return 0;
        }
        if (chunk.major != head->major || chunk.indefinite) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }
        if (*pos > (omc_u64)size || chunk.arg > ((omc_u64)size - *pos)
            || total > ((omc_u64)(~0U) - chunk.arg)) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }

        total += chunk.arg;
        if ((limit != 0U && total > limit) || total > (omc_u64)(~(omc_u32)0)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }

        if (ctx->store != (omc_store*)0) {
            omc_byte_ref chunk_ref;

            if (!omc_jumbf_store_bytes(ctx, bytes + (omc_size)*pos,
                                       (omc_size)chunk.arg, &chunk_ref)) {
                return 0;
            }
            if (!have_ref) {
                *out_ref = chunk_ref;
                have_ref = 1;
            } else {
                if ((omc_u64)chunk_ref.offset
                    != (omc_u64)out_ref->offset + (omc_u64)out_ref->size) {
                    ctx->res.status = OMC_JUMBF_MALFORMED;
                    return 0;
                }
                out_ref->size = (omc_u32)total;
            }
        } else {
            out_ref->size = (omc_u32)total;
        }
        *pos += chunk.arg;
        *out_size = (omc_u32)total;
    }
}

static int
omc_jumbf_cbor_skip_from_head(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                              omc_size size, omc_u64* pos, omc_u32 depth,
                              const omc_jumbf_cbor_head* head)
{
    omc_u64 count;
    omc_u64 i;

    if (ctx == (omc_jumbf_ctx*)0 || pos == (omc_u64*)0
        || head == (const omc_jumbf_cbor_head*)0) {
        return 0;
    }
    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }

    if (head->major == 0U || head->major == 1U || head->major == 7U) {
        return 1;
    }
    if (head->major == 2U || head->major == 3U) {
        omc_u64 max_len;

        max_len = (head->major == 2U) ? ctx->opts.limits.max_cbor_bytes_bytes
                                      : ctx->opts.limits.max_cbor_text_bytes;
        if (!head->indefinite) {
            if ((max_len != 0U && head->arg > max_len)
                || head->arg > (omc_u64)(~(omc_u32)0)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            if (*pos > (omc_u64)size || head->arg > ((omc_u64)size - *pos)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            *pos += head->arg;
            return 1;
        }

        count = 0U;
        while (!omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
            omc_jumbf_cbor_head chunk;

            if (!omc_jumbf_cbor_read_head(ctx, bytes, size, pos, &chunk)) {
                return 0;
            }
            if (chunk.indefinite || chunk.major != head->major) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            if (*pos > (omc_u64)size || chunk.arg > ((omc_u64)size - *pos)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            if (count > ((omc_u64)(~0U) - chunk.arg)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            count += chunk.arg;
            if ((max_len != 0U && count > max_len)
                || count > (omc_u64)(~(omc_u32)0)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *pos += chunk.arg;
        }
        return omc_jumbf_cbor_consume_break(bytes, size, pos);
    }
    if (head->major == 4U) {
        if (head->indefinite) {
            while (!omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                if (!omc_jumbf_cbor_skip_item(ctx, bytes, size, pos,
                                              depth + 1U)) {
                    return 0;
                }
            }
            return omc_jumbf_cbor_consume_break(bytes, size, pos);
        }
        count = head->arg;
        for (i = 0U; i < count; ++i) {
            if (!omc_jumbf_cbor_skip_item(ctx, bytes, size, pos, depth + 1U)) {
                return 0;
            }
        }
        return 1;
    }
    if (head->major == 5U) {
        if (head->indefinite) {
            while (!omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                if (!omc_jumbf_cbor_skip_item(ctx, bytes, size, pos,
                                              depth + 1U)
                    || !omc_jumbf_cbor_skip_item(ctx, bytes, size, pos,
                                                 depth + 1U)) {
                    return 0;
                }
            }
            return omc_jumbf_cbor_consume_break(bytes, size, pos);
        }
        count = head->arg;
        for (i = 0U; i < count; ++i) {
            if (!omc_jumbf_cbor_skip_item(ctx, bytes, size, pos, depth + 1U)
                || !omc_jumbf_cbor_skip_item(ctx, bytes, size, pos,
                                             depth + 1U)) {
                return 0;
            }
        }
        return 1;
    }
    if (head->major == 6U) {
        return omc_jumbf_cbor_skip_item(ctx, bytes, size, pos, depth + 1U);
    }
    ctx->res.status = OMC_JUMBF_MALFORMED;
    return 0;
}

static int
omc_jumbf_cbor_skip_item(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                         omc_size size, omc_u64* pos, omc_u32 depth)
{
    omc_jumbf_cbor_head head;

    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }
    if (!omc_jumbf_cbor_read_head(ctx, bytes, size, pos, &head)) {
        return 0;
    }
    return omc_jumbf_cbor_skip_from_head(ctx, bytes, size, pos, depth, &head);
}

static int
omc_jumbf_cbor_sanitize_segment(const omc_u8* bytes, omc_u32 size,
                                char* out, omc_u32 out_cap, omc_u32* out_len)
{
    omc_u32 i;
    omc_u32 len;

    if (out == (char*)0 || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }

    len = 0U;
    out[0] = '\0';
    for (i = 0U; i < size; ++i) {
        omc_u8 ch;
        char emit;

        ch = bytes[i];
        if (omc_jumbf_is_ascii_alnum(ch) || ch == (omc_u8)'-'
            || ch == (omc_u8)'_' || ch == (omc_u8)'.') {
            emit = (char)ch;
        } else {
            emit = '_';
        }

        if (emit == '_' && len != 0U && out[len - 1U] == '_') {
            continue;
        }
        if (!omc_jumbf_append_char(out, out_cap, &len, emit)) {
            return 0;
        }
    }

    if (len == 0U) {
        if (!omc_jumbf_append_mem(out, out_cap, &len, "key", 3U)) {
            return 0;
        }
    }

    *out_len = len;
    return 1;
}

static int
omc_jumbf_cbor_parse_key(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                         omc_size size, omc_u64* pos, omc_u32 depth,
                         omc_u32 map_index, char* out_seg, omc_u32 out_cap,
                         omc_u32* out_len)
{
    omc_jumbf_cbor_head head;
    omc_u64 max_key;

    if (ctx == (omc_jumbf_ctx*)0 || out_seg == (char*)0
        || out_len == (omc_u32*)0) {
        return 0;
    }
    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }
    if (!omc_jumbf_cbor_read_head(ctx, bytes, size, pos, &head)) {
        return 0;
    }

    max_key = ctx->opts.limits.max_cbor_key_bytes;
    if (max_key >= (omc_u64)out_cap) {
        max_key = (omc_u64)out_cap - 1U;
    }

    if (head.major == 3U) {
        return omc_jumbf_cbor_read_key_text(ctx, bytes, size, pos, &head,
                                            max_key, out_seg, out_cap,
                                            out_len);
    }
    if (head.major == 0U) {
        omc_u32 len;

        len = 0U;
        out_seg[0] = '\0';
        if (!omc_jumbf_append_u64_dec(out_seg, out_cap, &len, head.arg)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        *out_len = len;
        return 1;
    }
    if (head.major == 1U) {
        omc_u32 len;

        len = 0U;
        out_seg[0] = '\0';
        if (!omc_jumbf_append_char(out_seg, out_cap, &len, 'n')
            || !omc_jumbf_append_u64_dec(out_seg, out_cap, &len, head.arg)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        *out_len = len;
        return 1;
    }
    if (head.major == 7U) {
        omc_u32 len;

        if (head.indefinite) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }
        len = 0U;
        out_seg[0] = '\0';
        if (head.addl == 20U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "false", 5U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (head.addl == 21U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "true", 4U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (head.addl == 22U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "null", 4U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (head.addl == 23U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "undefined",
                                      9U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "simple", 6U)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        *out_len = len;
        return 1;
    }

    if (!omc_jumbf_cbor_skip_from_head(ctx, bytes, size, pos, depth + 1U,
                                       &head)) {
        return 0;
    }
    return omc_jumbf_make_synth_key_segment(
        map_index, omc_jumbf_cbor_major_suffix(head.major), out_seg, out_cap,
        out_len);
}

static int omc_jumbf_cbor_parse_item(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                     omc_size size, omc_u64* pos,
                                     omc_u32 depth, const char* path,
                                     omc_u32 path_len);

static int
omc_jumbf_cbor_parse_item_from_head(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                    omc_size size, omc_u64* pos,
                                    omc_u32 depth, const char* path,
                                    omc_u32 path_len,
                                    const omc_jumbf_cbor_head* head)
{
    omc_u32 payload_size;
    omc_u64 count;

    if (ctx == (omc_jumbf_ctx*)0 || head == (const omc_jumbf_cbor_head*)0) {
        return 0;
    }
    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }

    if (head->major == 0U) {
        return omc_jumbf_emit_cbor_u64(ctx, path, path_len, head->arg);
    }
    if (head->major == 1U) {
        omc_u64 max_arg;
        omc_s64 value;

        max_arg = ((((omc_u64)1U) << 63U) - 1U);
        if (head->arg >= max_arg) {
            char neg_text[64];
            omc_u32 neg_len;

            neg_len = 0U;
            neg_text[0] = '\0';
            if (!omc_jumbf_append_mem(neg_text, (omc_u32)sizeof(neg_text),
                                      &neg_len, "-(1+", 4U)
                || !omc_jumbf_append_u64_dec(neg_text,
                                             (omc_u32)sizeof(neg_text),
                                             &neg_len, head->arg)
                || !omc_jumbf_append_char(neg_text,
                                          (omc_u32)sizeof(neg_text),
                                          &neg_len, ')')) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            return omc_jumbf_emit_cbor_text(ctx, path, path_len,
                                            (const omc_u8*)neg_text, neg_len,
                                            OMC_TEXT_ASCII);
        }
        value = (omc_s64)-1 - (omc_s64)head->arg;
        return omc_jumbf_emit_cbor_i64(ctx, path, path_len, value);
    }
    if (head->major == 2U) {
        omc_byte_ref value_ref;

        if (!omc_jumbf_cbor_collect_byte_text_ref(
                ctx, bytes, size, pos, head,
                ctx->opts.limits.max_cbor_bytes_bytes, &value_ref,
                &payload_size)) {
            return 0;
        }
        if (ctx->store != (omc_store*)0) {
            return omc_jumbf_emit_cbor_ref(ctx, path, path_len, value_ref, 0,
                                           OMC_TEXT_UNKNOWN);
        }
        return omc_jumbf_emit_cbor_bytes(ctx, path, path_len,
                                         (const omc_u8*)0, payload_size);
    }
    if (head->major == 3U) {
        omc_byte_ref value_ref;

        if (!omc_jumbf_cbor_collect_byte_text_ref(
                ctx, bytes, size, pos, head,
                ctx->opts.limits.max_cbor_text_bytes, &value_ref,
                &payload_size)) {
            return 0;
        }
        if (ctx->store != (omc_store*)0) {
            omc_const_bytes value_view;

            value_view = omc_arena_view(&ctx->store->arena, value_ref);
            if (!omc_jumbf_bytes_valid_utf8(value_view.data,
                                            (omc_u32)value_view.size)) {
                return omc_jumbf_emit_cbor_ref(ctx, path, path_len, value_ref,
                                               0, OMC_TEXT_UNKNOWN);
            }
            return omc_jumbf_emit_cbor_ref(ctx, path, path_len, value_ref, 1,
                                           OMC_TEXT_UTF8);
        }
        return omc_jumbf_emit_cbor_text(ctx, path, path_len,
                                        (const omc_u8*)0, payload_size,
                                        OMC_TEXT_UTF8);
    }
    if (head->major == 4U) {
        char child_path[OMC_JUMBF_PATH_CAP];
        omc_u32 child_path_len;

        count = 0U;
        while (1) {
            if (head->indefinite) {
                if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                    return omc_jumbf_cbor_consume_break(bytes, size, pos);
                }
            } else if (count >= head->arg) {
                return 1;
            }

            if (!omc_jumbf_make_array_path(path, path_len, (omc_u32)count,
                                           child_path,
                                           (omc_u32)sizeof(child_path),
                                           &child_path_len)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            if (!omc_jumbf_cbor_parse_item(ctx, bytes, size, pos, depth + 1U,
                                           child_path, child_path_len)) {
                return 0;
            }
            count += 1U;
        }
    }
    if (head->major == 5U) {
        omc_u64 map_index;

        map_index = 0U;
        while (1) {
            char seg[OMC_JUMBF_PATH_CAP];
            char child_path[OMC_JUMBF_PATH_CAP];
            omc_u32 seg_len;
            omc_u32 child_path_len;

            if (head->indefinite) {
                if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                    return omc_jumbf_cbor_consume_break(bytes, size, pos);
                }
            } else if (map_index >= head->arg) {
                return 1;
            }

            if (!omc_jumbf_cbor_parse_key(ctx, bytes, size, pos, depth + 1U,
                                          (omc_u32)map_index, seg,
                                          (omc_u32)sizeof(seg), &seg_len)) {
                return 0;
            }
            if (!omc_jumbf_make_path_with_segment(path, path_len, seg, seg_len,
                                                  child_path,
                                                  (omc_u32)sizeof(child_path),
                                                  &child_path_len)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            if (!omc_jumbf_cbor_parse_item(ctx, bytes, size, pos, depth + 1U,
                                           child_path, child_path_len)) {
                return 0;
            }
            map_index += 1U;
        }
    }
    if (head->major == 6U) {
        char tag_path[OMC_JUMBF_PATH_CAP];
        omc_u32 tag_path_len;

        if (!omc_jumbf_make_path_with_segment(path, path_len, "@tag", 4U,
                                              tag_path,
                                              (omc_u32)sizeof(tag_path),
                                              &tag_path_len)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        if (!omc_jumbf_emit_cbor_u64(ctx, tag_path, tag_path_len, head->arg)) {
            return 0;
        }
        return omc_jumbf_cbor_parse_item(ctx, bytes, size, pos, depth + 1U,
                                         path, path_len);
    }
    if (head->major == 7U) {
        if (head->indefinite) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }
        if (head->addl <= 19U) {
            return omc_jumbf_emit_cbor_u8(ctx, path, path_len,
                                          (omc_u8)head->addl);
        }
        if (head->addl == 20U) {
            return omc_jumbf_emit_cbor_u8(ctx, path, path_len, 0U);
        }
        if (head->addl == 21U) {
            return omc_jumbf_emit_cbor_u8(ctx, path, path_len, 1U);
        }
        if (head->addl == 22U) {
            return omc_jumbf_emit_cbor_text(ctx, path, path_len,
                                            (const omc_u8*)"null", 4U,
                                            OMC_TEXT_ASCII);
        }
        if (head->addl == 23U) {
            return omc_jumbf_emit_cbor_text(ctx, path, path_len,
                                            (const omc_u8*)"undefined", 9U,
                                            OMC_TEXT_ASCII);
        }
        if (head->addl == 24U) {
            return omc_jumbf_emit_cbor_u8(ctx, path, path_len,
                                          (omc_u8)head->arg);
        }
        if (head->addl == 25U) {
            return omc_jumbf_emit_cbor_f32_bits(
                ctx, path, path_len,
                omc_jumbf_cbor_half_to_f32_bits((omc_u16)head->arg));
        }
        if (head->addl == 26U) {
            return omc_jumbf_emit_cbor_f32_bits(ctx, path, path_len,
                                                (omc_u32)head->arg);
        }
        if (head->addl == 27U) {
            return omc_jumbf_emit_cbor_f64_bits(ctx, path, path_len,
                                                head->arg);
        }
        return omc_jumbf_emit_cbor_text(ctx, path, path_len,
                                        (const omc_u8*)"simple", 6U,
                                        OMC_TEXT_ASCII);
    }

    ctx->res.status = OMC_JUMBF_MALFORMED;
    return 0;
}

static int
omc_jumbf_cbor_parse_item(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                          omc_size size, omc_u64* pos, omc_u32 depth,
                          const char* path, omc_u32 path_len)
{
    omc_jumbf_cbor_head head;

    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }
    if (!omc_jumbf_cbor_read_head(ctx, bytes, size, pos, &head)) {
        return 0;
    }
    return omc_jumbf_cbor_parse_item_from_head(ctx, bytes, size, pos, depth,
                                               path, path_len, &head);
}

static int
omc_jumbf_decode_cbor_payload(omc_jumbf_ctx* ctx, const omc_u8* payload,
                              omc_size payload_size, const char* prefix,
                              omc_u32 prefix_len)
{
    omc_u64 pos;

    pos = 0U;
    while (pos < (omc_u64)payload_size) {
        if (!omc_jumbf_cbor_parse_item(ctx, payload, payload_size, &pos, 0U,
                                       prefix, prefix_len)) {
            return 0;
        }
    }
    return 1;
}

static int omc_jumbf_cbor_skip_item_raw(omc_jumbf_ctx* ctx,
                                        const omc_u8* bytes, omc_size size,
                                        omc_u64* pos, omc_u32 depth);

static int
omc_jumbf_cbor_decode_i64(const omc_jumbf_cbor_head* head, omc_s64* out_value)
{
    omc_u64 max_i64;

    if (head == (const omc_jumbf_cbor_head*)0 || out_value == (omc_s64*)0
        || head->indefinite) {
        return 0;
    }

    max_i64 = (((omc_u64)1U) << 63U) - 1U;
    if (head->major == 0U) {
        if (head->arg > max_i64) {
            return 0;
        }
        *out_value = (omc_s64)head->arg;
        return 1;
    }
    if (head->major == 1U) {
        if (head->arg > max_i64) {
            return 0;
        }
        *out_value = (omc_s64)-1 - (omc_s64)head->arg;
        return 1;
    }
    return 0;
}

static int
omc_jumbf_cose_alg_name(omc_s64 alg, char* out, omc_u32 out_cap,
                        omc_u32* out_len)
{
    const char* name;

    name = (const char*)0;
    switch (alg) {
        case (omc_s64)-7: name = "es256"; break;
        case (omc_s64)-35: name = "es384"; break;
        case (omc_s64)-36: name = "es512"; break;
        case (omc_s64)-257: name = "rs256"; break;
        case (omc_s64)-258: name = "rs384"; break;
        case (omc_s64)-259: name = "rs512"; break;
        case (omc_s64)-37: name = "ps256"; break;
        case (omc_s64)-38: name = "ps384"; break;
        case (omc_s64)-39: name = "ps512"; break;
        case (omc_s64)-8: name = "eddsa"; break;
        default: break;
    }
    if (name == (const char*)0 || out == (char*)0 || out_len == (omc_u32*)0) {
        return 0;
    }
    *out_len = 0U;
    out[0] = '\0';
    return omc_jumbf_append_mem(out, out_cap, out_len, name,
                                omc_jumbf_cstr_size(name));
}

static int
omc_jumbf_cbor_read_small_text(const omc_u8* bytes, omc_size size,
                               omc_u64* pos, const omc_jumbf_cbor_head* head,
                               omc_u64 max_total, char* out, omc_u32 out_cap,
                               omc_u32* out_len)
{
    omc_u64 total;

    if (bytes == (const omc_u8*)0 || pos == (omc_u64*)0
        || head == (const omc_jumbf_cbor_head*)0 || out == (char*)0
        || out_len == (omc_u32*)0 || out_cap == 0U || head->major != 3U) {
        return 0;
    }

    total = 0U;
    out[0] = '\0';
    if (!head->indefinite) {
        const omc_u8* payload;
        omc_u32 payload_size;

        if (!omc_jumbf_cbor_read_definite_payload(bytes, size, pos, head,
                                                  max_total, &payload,
                                                  &payload_size)
            || payload_size + 1U > out_cap) {
            return 0;
        }
        memcpy(out, payload, payload_size);
        out[payload_size] = '\0';
        *out_len = payload_size;
        return 1;
    }

    while (1) {
        omc_jumbf_cbor_head chunk;

        if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
            if (!omc_jumbf_cbor_consume_break(bytes, size, pos)
                || total > (omc_u64)(out_cap - 1U)) {
                return 0;
            }
            out[(omc_u32)total] = '\0';
            *out_len = (omc_u32)total;
            return 1;
        }
        if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, &chunk)
            || chunk.major != 3U || chunk.indefinite
            || *pos > (omc_u64)size || chunk.arg > ((omc_u64)size - *pos)
            || total > ((omc_u64)(~0U) - chunk.arg)
            || total + chunk.arg > max_total
            || total + chunk.arg > (omc_u64)(out_cap - 1U)) {
            return 0;
        }
        memcpy(out + (omc_u32)total, bytes + (omc_size)*pos,
               (omc_size)chunk.arg);
        total += chunk.arg;
        *pos += chunk.arg;
    }
}

static int
omc_jumbf_cbor_skip_from_head_raw(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                  omc_size size, omc_u64* pos, omc_u32 depth,
                                  const omc_jumbf_cbor_head* head)
{
    omc_u64 count;
    omc_u64 i;

    if (ctx == (omc_jumbf_ctx*)0 || pos == (omc_u64*)0
        || head == (const omc_jumbf_cbor_head*)0) {
        return 0;
    }
    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }

    if (head->major == 0U || head->major == 1U || head->major == 7U) {
        return 1;
    }
    if (head->major == 2U || head->major == 3U) {
        if (!head->indefinite) {
            if (*pos > (omc_u64)size || head->arg > ((omc_u64)size - *pos)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            *pos += head->arg;
            return 1;
        }
        while (!omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
            omc_jumbf_cbor_head chunk;

            if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, &chunk)
                || chunk.indefinite || chunk.major != head->major
                || *pos > (omc_u64)size || chunk.arg > ((omc_u64)size - *pos)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            *pos += chunk.arg;
        }
        if (!omc_jumbf_cbor_consume_break(bytes, size, pos)) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }
        return 1;
    }
    if (head->major == 4U) {
        if (head->indefinite) {
            while (!omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                if (!omc_jumbf_cbor_skip_item_raw(ctx, bytes, size, pos,
                                                  depth + 1U)) {
                    return 0;
                }
            }
            if (!omc_jumbf_cbor_consume_break(bytes, size, pos)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            return 1;
        }
        count = head->arg;
        for (i = 0U; i < count; ++i) {
            if (!omc_jumbf_cbor_skip_item_raw(ctx, bytes, size, pos,
                                              depth + 1U)) {
                return 0;
            }
        }
        return 1;
    }
    if (head->major == 5U) {
        if (head->indefinite) {
            while (!omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                if (!omc_jumbf_cbor_skip_item_raw(ctx, bytes, size, pos,
                                                  depth + 1U)
                    || !omc_jumbf_cbor_skip_item_raw(ctx, bytes, size, pos,
                                                     depth + 1U)) {
                    return 0;
                }
            }
            if (!omc_jumbf_cbor_consume_break(bytes, size, pos)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
            return 1;
        }
        count = head->arg;
        for (i = 0U; i < count; ++i) {
            if (!omc_jumbf_cbor_skip_item_raw(ctx, bytes, size, pos,
                                              depth + 1U)
                || !omc_jumbf_cbor_skip_item_raw(ctx, bytes, size, pos,
                                                 depth + 1U)) {
                return 0;
            }
        }
        return 1;
    }
    if (head->major == 6U) {
        return omc_jumbf_cbor_skip_item_raw(ctx, bytes, size, pos,
                                            depth + 1U);
    }

    ctx->res.status = OMC_JUMBF_MALFORMED;
    return 0;
}

static int
omc_jumbf_cbor_skip_item_raw(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                             omc_size size, omc_u64* pos, omc_u32 depth)
{
    omc_jumbf_cbor_head head;

    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }
    if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, &head)) {
        ctx->res.status = OMC_JUMBF_MALFORMED;
        return 0;
    }
    return omc_jumbf_cbor_skip_from_head_raw(ctx, bytes, size, pos, depth,
                                             &head);
}

static int
omc_jumbf_cose_parse_protected_header(omc_jumbf_ctx* ctx,
                                      omc_const_bytes protected_bytes,
                                      omc_jumbf_sig_proj* sig)
{
    omc_u64 pos;
    omc_jumbf_cbor_head head;

    if (ctx == (omc_jumbf_ctx*)0 || sig == (omc_jumbf_sig_proj*)0
        || protected_bytes.data == (const omc_u8*)0
        || protected_bytes.size == 0U) {
        return 0;
    }
    if (protected_bytes.size > ((omc_size)1U << 20U)) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }

    pos = 0U;
    if (!omc_jumbf_cbor_read_head_raw(protected_bytes.data,
                                      protected_bytes.size, &pos, &head)
        || head.major != 5U) {
        return 0;
    }

    while (1) {
        omc_jumbf_cbor_head key_head;
        omc_jumbf_cbor_head value_head;
        int key_is_alg;

        if (head.indefinite) {
            if (omc_jumbf_cbor_peek_break(protected_bytes.data,
                                          protected_bytes.size, pos)) {
                return 1;
            }
        } else if (head.arg == 0U) {
            return 1;
        }

        if (!omc_jumbf_cbor_read_head_raw(protected_bytes.data,
                                          protected_bytes.size, &pos,
                                          &key_head)
            || !omc_jumbf_cbor_read_head_raw(protected_bytes.data,
                                             protected_bytes.size, &pos,
                                             &value_head)) {
            return 0;
        }

        key_is_alg = 0;
        if (key_head.major == 0U && !key_head.indefinite && key_head.arg == 1U) {
            key_is_alg = 1;
        } else if (key_head.major == 3U) {
            char key_text[32];
            omc_u32 key_len;

            if (!omc_jumbf_cbor_read_small_text(protected_bytes.data,
                                                protected_bytes.size, &pos,
                                                &key_head, 31U, key_text,
                                                (omc_u32)sizeof(key_text),
                                                &key_len)) {
                return 0;
            }
            key_is_alg = omc_jumbf_cstr_eq(key_text, "alg")
                         || omc_jumbf_cstr_eq(key_text, "algorithm");
        } else {
            if (!omc_jumbf_cbor_skip_from_head_raw(ctx, protected_bytes.data,
                                                   protected_bytes.size, &pos,
                                                   0U, &key_head)) {
                return 0;
            }
        }

        if (!key_is_alg) {
            if (!omc_jumbf_cbor_skip_from_head_raw(ctx, protected_bytes.data,
                                                   protected_bytes.size, &pos,
                                                   0U, &value_head)) {
                return 0;
            }
        } else if ((value_head.major == 0U || value_head.major == 1U)
                   && !value_head.indefinite) {
            omc_s64 alg;

            if (omc_jumbf_cbor_decode_i64(&value_head, &alg)
                && omc_jumbf_cose_alg_name(alg, sig->algorithm,
                                           (omc_u32)sizeof(sig->algorithm),
                                           &sig->algorithm_len)) {
                sig->has_algorithm = 1;
            }
            return 1;
        } else if (value_head.major == 3U) {
            char alg_text[32];

            if (!omc_jumbf_cbor_read_small_text(protected_bytes.data,
                                                protected_bytes.size, &pos,
                                                &value_head, 31U, alg_text,
                                                (omc_u32)sizeof(alg_text),
                                                &sig->algorithm_len)) {
                return 0;
            }
            memcpy(sig->algorithm, alg_text, sig->algorithm_len + 1U);
            sig->has_algorithm = (sig->algorithm_len != 0U);
            return 1;
        } else {
            return 0;
        }

        if (!head.indefinite) {
            head.arg -= 1U;
        }
    }
}

static int
omc_jumbf_cose_parse_unprotected_header(omc_jumbf_ctx* ctx,
                                        const omc_u8* bytes, omc_size size,
                                        omc_u64* pos,
                                        const omc_jumbf_cbor_head* head,
                                        omc_jumbf_sig_proj* sig)
{
    omc_u64 pairs;

    if (ctx == (omc_jumbf_ctx*)0 || pos == (omc_u64*)0
        || head == (const omc_jumbf_cbor_head*)0
        || sig == (omc_jumbf_sig_proj*)0 || head->major != 5U) {
        return 0;
    }

    pairs = head->indefinite ? (~(omc_u64)0) : head->arg;
    while (1) {
        omc_jumbf_cbor_head key_head;
        omc_jumbf_cbor_head value_head;
        int key_is_alg;
        int key_is_x5chain;
        int key_is_public_der;
        int key_is_certificate_der;

        if (head->indefinite) {
            if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                return omc_jumbf_cbor_consume_break(bytes, size, pos);
            }
        } else if (pairs == 0U) {
            return 1;
        }

        if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, &key_head)
            || !omc_jumbf_cbor_read_head_raw(bytes, size, pos, &value_head)) {
            return 0;
        }

        key_is_alg = 0;
        key_is_x5chain = 0;
        key_is_public_der = 0;
        key_is_certificate_der = 0;

        if (key_head.major == 0U || key_head.major == 1U) {
            omc_s64 key_int;

            if (!omc_jumbf_cbor_decode_i64(&key_head, &key_int)) {
                return 0;
            }
            key_is_alg = (key_int == 1);
            key_is_x5chain = (key_int == 33);
        } else if (key_head.major == 3U) {
            char key_text[64];
            omc_u32 key_len;

            if (!omc_jumbf_cbor_read_small_text(bytes, size, pos, &key_head,
                                                63U, key_text,
                                                (omc_u32)sizeof(key_text),
                                                &key_len)) {
                return 0;
            }
            key_is_alg = omc_jumbf_cstr_eq(key_text, "1")
                         || omc_jumbf_cstr_eq(key_text, "alg")
                         || omc_jumbf_cstr_eq(key_text, "algorithm");
            key_is_x5chain = omc_jumbf_cstr_eq(key_text, "33")
                             || omc_jumbf_cstr_eq(key_text, "x5chain")
                             || omc_jumbf_cstr_eq(key_text, "x5c");
            key_is_public_der = omc_jumbf_cstr_eq(key_text, "public_key_der")
                                || omc_jumbf_cstr_eq(key_text, "public_key");
            key_is_certificate_der
                = omc_jumbf_cstr_eq(key_text, "certificate_der")
                  || omc_jumbf_cstr_eq(key_text, "certificate");
        } else {
            if (!omc_jumbf_cbor_skip_from_head_raw(ctx, bytes, size, pos, 0U,
                                                   &key_head)) {
                return 0;
            }
        }

        if (key_is_alg && !sig->has_unprotected_alg_int) {
            omc_s64 alg;

            if (omc_jumbf_cbor_decode_i64(&value_head, &alg)) {
                sig->unprotected_alg_int = alg;
                sig->has_unprotected_alg_int = 1;
                if (!sig->has_algorithm
                    && omc_jumbf_cose_alg_name(alg, sig->algorithm,
                                               (omc_u32)sizeof(sig->algorithm),
                                               &sig->algorithm_len)) {
                    sig->has_algorithm = 1;
                }
                goto next_pair;
            }
        }

        if (key_is_x5chain) {
            if (value_head.major == 2U) {
                omc_byte_ref cert_ref;
                omc_u32 cert_size;

                if (!omc_jumbf_cbor_collect_byte_text_ref(
                        ctx, bytes, size, pos, &value_head,
                        ctx->opts.limits.max_cbor_bytes_bytes, &cert_ref,
                        &cert_size)) {
                    return 0;
                }
                sig->x5chain_count += 1U;
                if (!sig->has_certificate_ref && cert_size != 0U) {
                    sig->certificate_ref = cert_ref;
                    sig->has_certificate_ref = 1;
                }
                goto next_pair;
            }
            if (value_head.major == 4U) {
                omc_u64 count;

                count = value_head.indefinite ? (~(omc_u64)0) : value_head.arg;
                while (1) {
                    omc_jumbf_cbor_head elem;
                    omc_byte_ref cert_ref;
                    omc_u32 cert_size;

                    if (value_head.indefinite) {
                        if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                            if (!omc_jumbf_cbor_consume_break(bytes, size,
                                                              pos)) {
                                return 0;
                            }
                            break;
                        }
                    } else if (count == 0U) {
                        break;
                    }

                    if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, &elem)) {
                        return 0;
                    }
                    if (elem.major != 2U) {
                        if (!omc_jumbf_cbor_skip_from_head_raw(ctx, bytes, size,
                                                               pos, 1U, &elem)) {
                            return 0;
                        }
                    } else {
                        if (!omc_jumbf_cbor_collect_byte_text_ref(
                                ctx, bytes, size, pos, &elem,
                                ctx->opts.limits.max_cbor_bytes_bytes,
                                &cert_ref, &cert_size)) {
                            return 0;
                        }
                        sig->x5chain_count += 1U;
                        if (!sig->has_certificate_ref && cert_size != 0U) {
                            sig->certificate_ref = cert_ref;
                            sig->has_certificate_ref = 1;
                        }
                    }
                    if (!value_head.indefinite) {
                        count -= 1U;
                    }
                }
                goto next_pair;
            }
        }

        if (key_is_public_der && !sig->has_public_key_ref
            && value_head.major == 2U) {
            omc_u32 value_size;

            if (!omc_jumbf_cbor_collect_byte_text_ref(
                    ctx, bytes, size, pos, &value_head,
                    ctx->opts.limits.max_cbor_bytes_bytes,
                    &sig->public_key_ref, &value_size)) {
                return 0;
            }
            sig->has_public_key_ref = (value_size != 0U);
            goto next_pair;
        }

        if (key_is_certificate_der && !sig->has_certificate_ref
            && value_head.major == 2U) {
            omc_u32 value_size;

            if (!omc_jumbf_cbor_collect_byte_text_ref(
                    ctx, bytes, size, pos, &value_head,
                    ctx->opts.limits.max_cbor_bytes_bytes,
                    &sig->certificate_ref, &value_size)) {
                return 0;
            }
            sig->has_certificate_ref = (value_size != 0U);
            goto next_pair;
        }

        if (!omc_jumbf_cbor_skip_from_head_raw(ctx, bytes, size, pos, 0U,
                                               &value_head)) {
            return 0;
        }

next_pair:
        if (!head->indefinite) {
            pairs -= 1U;
        }
    }
}

static int
omc_jumbf_cose_parse_sign1_bytes(omc_jumbf_ctx* ctx, omc_const_bytes cose_bytes,
                                 omc_jumbf_sig_proj* sig)
{
    omc_u64 pos;
    omc_jumbf_cbor_head head;
    omc_const_bytes protected_inline;

    if (ctx == (omc_jumbf_ctx*)0 || sig == (omc_jumbf_sig_proj*)0
        || cose_bytes.data == (const omc_u8*)0) {
        return 0;
    }

    pos = 0U;
    protected_inline = omc_jumbf_const_bytes((const void*)0, 0U);
    if (!omc_jumbf_cbor_read_head_raw(cose_bytes.data, cose_bytes.size, &pos,
                                      &head)) {
        return 0;
    }
    if (head.major == 6U && !head.indefinite && head.arg == 18U) {
        if (!omc_jumbf_cbor_read_head_raw(cose_bytes.data, cose_bytes.size,
                                          &pos, &head)) {
            return 0;
        }
    }
    if (head.major != 4U) {
        return 0;
    }
    if (!head.indefinite && head.arg != 4U) {
        return 0;
    }

    {
        omc_jumbf_cbor_head protected_head;
        omc_jumbf_cbor_head unprotected_head;
        omc_jumbf_cbor_head payload_head;
        omc_jumbf_cbor_head signature_head;
        omc_u32 value_size;

        if (!omc_jumbf_cbor_read_head_raw(cose_bytes.data, cose_bytes.size, &pos,
                                          &protected_head)
            || protected_head.major != 2U) {
            return 0;
        }
        if (!protected_head.indefinite
            && pos <= (omc_u64)cose_bytes.size
            && protected_head.arg <= ((omc_u64)cose_bytes.size - pos)
            && protected_head.arg <= (omc_u64)(~(omc_u32)0)) {
            protected_inline = omc_jumbf_const_bytes(
                cose_bytes.data + (omc_size)pos, (omc_size)protected_head.arg);
        }
        if (!omc_jumbf_cbor_collect_byte_text_ref(
                ctx, cose_bytes.data, cose_bytes.size, &pos, &protected_head,
                ctx->opts.limits.max_cbor_bytes_bytes, &sig->protected_ref,
                &value_size)) {
            return 0;
        }
        sig->has_protected_ref = 1;

        if (!omc_jumbf_cbor_read_head_raw(cose_bytes.data, cose_bytes.size, &pos,
                                          &unprotected_head)
            || unprotected_head.major != 5U) {
            return 0;
        }
        if (!omc_jumbf_cose_parse_unprotected_header(ctx, cose_bytes.data,
                                                     cose_bytes.size, &pos,
                                                     &unprotected_head, sig)) {
            return 0;
        }

        if (!omc_jumbf_cbor_read_head_raw(cose_bytes.data, cose_bytes.size, &pos,
                                          &payload_head)) {
            return 0;
        }
        if (payload_head.major == 2U) {
            if (!omc_jumbf_cbor_collect_byte_text_ref(
                    ctx, cose_bytes.data, cose_bytes.size, &pos, &payload_head,
                    ctx->opts.limits.max_cbor_bytes_bytes, &sig->payload_ref,
                    &value_size)) {
                return 0;
            }
            sig->has_payload_ref = 1;
            sig->payload_is_null = 0;
        } else if (payload_head.major == 7U && !payload_head.indefinite
                   && payload_head.addl == 22U) {
            sig->payload_is_null = 1;
        } else {
            return 0;
        }

        if (!omc_jumbf_cbor_read_head_raw(cose_bytes.data, cose_bytes.size, &pos,
                                          &signature_head)
            || signature_head.major != 2U) {
            return 0;
        }
        if (!omc_jumbf_cbor_collect_byte_text_ref(
                ctx, cose_bytes.data, cose_bytes.size, &pos, &signature_head,
                ctx->opts.limits.max_cbor_bytes_bytes, &sig->signature_ref,
                &value_size)) {
            return 0;
        }
        sig->has_signature_ref = 1;
    }

    if (head.indefinite) {
        if (!omc_jumbf_cbor_consume_break(cose_bytes.data, cose_bytes.size,
                                          &pos)) {
            return 0;
        }
    }
    if (pos != (omc_u64)cose_bytes.size) {
        return 0;
    }
    if (!sig->has_algorithm && sig->has_protected_ref
        && ctx->store != (omc_store*)0) {
        omc_const_bytes protected_view;

        protected_view = omc_arena_view(&ctx->store->arena, sig->protected_ref);
        (void)omc_jumbf_cose_parse_protected_header(ctx, protected_view, sig);
    } else if (!sig->has_algorithm && protected_inline.data != (const omc_u8*)0) {
        (void)omc_jumbf_cose_parse_protected_header(ctx, protected_inline, sig);
    }
    return 1;
}

static int
omc_jumbf_emit_indexed_field_text(omc_jumbf_ctx* ctx, const char* root,
                                  omc_u32 index, const char* suffix,
                                  const char* value, omc_u32 value_len)
{
    char field[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    len = 0U;
    field[0] = '\0';
    if (!omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_u32_dec(field, (omc_u32)sizeof(field), &len, index)
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, suffix,
                                 omc_jumbf_cstr_size(suffix))) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return omc_jumbf_emit_field_text(ctx, field, len, value, value_len);
}

static int
omc_jumbf_emit_indexed_field_bytes_ref(omc_jumbf_ctx* ctx, const char* root,
                                       omc_u32 index, const char* suffix,
                                       omc_byte_ref value_ref)
{
    char field[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    len = 0U;
    field[0] = '\0';
    if (!omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_u32_dec(field, (omc_u32)sizeof(field), &len, index)
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, suffix,
                                 omc_jumbf_cstr_size(suffix))) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return omc_jumbf_emit_field_bytes_ref(ctx, field, len, value_ref);
}

static int
omc_jumbf_emit_indexed_field_u64(omc_jumbf_ctx* ctx, const char* root,
                                 omc_u32 index, const char* suffix,
                                 omc_u64 value)
{
    char field[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    len = 0U;
    field[0] = '\0';
    if (!omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_u32_dec(field, (omc_u32)sizeof(field), &len, index)
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, suffix,
                                 omc_jumbf_cstr_size(suffix))) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return omc_jumbf_emit_field_u64(ctx, field, len, value);
}

static int
omc_jumbf_emit_indexed_field_i64(omc_jumbf_ctx* ctx, const char* root,
                                 omc_u32 index, const char* suffix,
                                 omc_s64 value)
{
    char field[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    len = 0U;
    field[0] = '\0';
    if (!omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_u32_dec(field, (omc_u32)sizeof(field), &len, index)
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, suffix,
                                 omc_jumbf_cstr_size(suffix))) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return omc_jumbf_emit_field_i64(ctx, field, len, value);
}

static int
omc_jumbf_emit_indexed_field_u8(omc_jumbf_ctx* ctx, const char* root,
                                omc_u32 index, const char* suffix,
                                omc_u8 value)
{
    char field[OMC_JUMBF_PATH_CAP];
    omc_u32 len;

    len = 0U;
    field[0] = '\0';
    if (!omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, root,
                              omc_jumbf_cstr_size(root))
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_u32_dec(field, (omc_u32)sizeof(field), &len, index)
        || !omc_jumbf_append_char(field, (omc_u32)sizeof(field), &len, '.')
        || !omc_jumbf_append_mem(field, (omc_u32)sizeof(field), &len, suffix,
                                 omc_jumbf_cstr_size(suffix))) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }
    return omc_jumbf_emit_field_u8(ctx, field, len, value);
}

static void
omc_jumbf_sig_proj_init(omc_jumbf_sig_proj* sig)
{
    if (sig == (omc_jumbf_sig_proj*)0) {
        return;
    }
    memset(sig, 0, sizeof(*sig));
}

static int
omc_jumbf_collect_signature_projection(omc_jumbf_ctx* ctx, omc_size scan_limit,
                                       const char* prefix, omc_u32 prefix_len,
                                       omc_jumbf_sig_proj* out_sig)
{
    omc_size i;
    char unprotected_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 unprotected_prefix_len;
    omc_u32 tmp_pos;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || prefix == (const char*)0 || out_sig == (omc_jumbf_sig_proj*)0) {
        return 0;
    }

    omc_jumbf_sig_proj_init(out_sig);
    memcpy(out_sig->prefix, prefix, prefix_len);
    out_sig->prefix[prefix_len] = '\0';
    out_sig->prefix_len = prefix_len;
    unprotected_prefix_len = 0U;
    unprotected_prefix[0] = '\0';
    (void)omc_jumbf_append_mem(unprotected_prefix,
                               (omc_u32)sizeof(unprotected_prefix),
                               &unprotected_prefix_len, prefix, prefix_len);
    (void)omc_jumbf_append_mem(unprotected_prefix,
                               (omc_u32)sizeof(unprotected_prefix),
                               &unprotected_prefix_len, "[1]", 3U);
    out_sig->linked_to_claim
        = (omc_jumbf_view_find(omc_jumbf_const_bytes(prefix, prefix_len), ".claims[",
                               &tmp_pos) != 0);
    if (omc_jumbf_view_find(omc_jumbf_const_bytes(prefix, prefix_len),
                            ".signatures[", &tmp_pos)
        && tmp_pos != 0U
        && tmp_pos + 1U < (omc_u32)sizeof(out_sig->direct_claim_prefix)) {
        memcpy(out_sig->direct_claim_prefix, prefix, tmp_pos);
        out_sig->direct_claim_prefix[tmp_pos] = '\0';
        out_sig->direct_claim_prefix_len = tmp_pos;
        out_sig->has_direct_claim_prefix = 1;
    }

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        omc_const_bytes value_view;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (!omc_jumbf_view_starts_with(key, prefix)
            || (key.size != prefix_len
                && key.data[prefix_len] != (omc_u8)'.'
                && key.data[prefix_len] != (omc_u8)'[')) {
            continue;
        }

        out_sig->key_hits += 1U;
        value_view = omc_jumbf_entry_value_view(ctx->store, entry);

        if (entry->value.kind == OMC_VAL_TEXT
            && (omc_jumbf_key_matches_field(key, prefix, prefix_len, "alg")
                || omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                               "algorithm"))
            && !out_sig->has_algorithm
            && value_view.size < (omc_size)sizeof(out_sig->algorithm)
            && omc_jumbf_bytes_all_ascii_printable(value_view.data,
                                                   (omc_u32)value_view.size)) {
            memcpy(out_sig->algorithm, value_view.data, value_view.size);
            out_sig->algorithm[value_view.size] = '\0';
            out_sig->algorithm_len = (omc_u32)value_view.size;
            out_sig->has_algorithm = (out_sig->algorithm_len != 0U);
        }
        if (entry->value.kind == OMC_VAL_TEXT
            && !omc_jumbf_sig_proj_note_ref_text(
                   out_sig, key, (const char*)value_view.data,
                   (omc_u32)value_view.size)) {
            return 0;
        }

        if (entry->value.kind == OMC_VAL_BYTES || entry->value.kind == OMC_VAL_TEXT) {
            if (key.size == prefix_len && memcmp(key.data, prefix, prefix_len) == 0
                && entry->value.kind == OMC_VAL_BYTES) {
                (void)omc_jumbf_cose_parse_sign1_bytes(ctx, value_view, out_sig);
            } else if (omc_jumbf_key_is_indexed_item(key, prefix, prefix_len, 0U)
                       && entry->value.kind == OMC_VAL_BYTES
                       && !out_sig->has_protected_ref) {
                out_sig->protected_ref = entry->value.u.ref;
                out_sig->has_protected_ref = 1;
            } else if (omc_jumbf_key_is_indexed_item(key, prefix, prefix_len, 2U)) {
                if (entry->value.kind == OMC_VAL_BYTES && !out_sig->has_payload_ref) {
                    out_sig->payload_ref = entry->value.u.ref;
                    out_sig->has_payload_ref = 1;
                    out_sig->payload_is_null = 0;
                } else if (entry->value.kind == OMC_VAL_TEXT
                           && omc_jumbf_view_eq_cstr(value_view, "null")) {
                    out_sig->payload_is_null = 1;
                }
            } else if (omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                                   "payload")) {
                if (entry->value.kind == OMC_VAL_BYTES && !out_sig->has_payload_ref) {
                    out_sig->payload_ref = entry->value.u.ref;
                    out_sig->has_payload_ref = 1;
                    out_sig->payload_is_null = 0;
                } else if (entry->value.kind == OMC_VAL_TEXT
                           && omc_jumbf_view_eq_cstr(value_view, "null")) {
                    out_sig->payload_is_null = 1;
                }
            } else if (omc_jumbf_key_is_indexed_item(key, prefix, prefix_len, 3U)
                       && entry->value.kind == OMC_VAL_BYTES
                       && !out_sig->has_signature_ref) {
                out_sig->signature_ref = entry->value.u.ref;
                out_sig->has_signature_ref = 1;
            } else if ((omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                                    "signature")
                        || omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                                       "sig"))
                       && entry->value.kind == OMC_VAL_BYTES
                       && !out_sig->has_signature_ref) {
                out_sig->signature_ref = entry->value.u.ref;
                out_sig->has_signature_ref = 1;
            } else if ((omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                                    "public_key_der")
                        || omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                                       "public_key")
                        || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                                       unprotected_prefix_len,
                                                       "public_key_der")
                        || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                                       unprotected_prefix_len,
                                                       "public_key"))
                       && entry->value.kind == OMC_VAL_BYTES
                       && !out_sig->has_public_key_ref) {
                out_sig->public_key_ref = entry->value.u.ref;
                out_sig->has_public_key_ref = 1;
            } else if ((omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                                    "certificate_der")
                        || omc_jumbf_key_matches_field(key, prefix, prefix_len,
                                                       "certificate")
                        || omc_jumbf_key_matches_field(
                               key, unprotected_prefix, unprotected_prefix_len,
                               "certificate_der")
                        || omc_jumbf_key_matches_field(
                               key, unprotected_prefix, unprotected_prefix_len,
                               "certificate"))
                       && entry->value.kind == OMC_VAL_BYTES
                       && !out_sig->has_certificate_ref) {
                out_sig->certificate_ref = entry->value.u.ref;
                out_sig->has_certificate_ref = 1;
            }
        }

        if (!out_sig->has_unprotected_alg_int
            && entry->value.kind == OMC_VAL_SCALAR
            && omc_jumbf_key_matches_field(key, unprotected_prefix,
                                           unprotected_prefix_len, "1")) {
            if (entry->value.elem_type == OMC_ELEM_I64) {
                out_sig->unprotected_alg_int = entry->value.u.i64;
                out_sig->has_unprotected_alg_int = 1;
            } else if (entry->value.elem_type == OMC_ELEM_U64) {
                out_sig->unprotected_alg_int = (omc_s64)entry->value.u.u64;
                out_sig->has_unprotected_alg_int = 1;
            }
        }
        if (entry->value.kind == OMC_VAL_SCALAR
            && !omc_jumbf_sig_proj_note_ref_scalar(
                   out_sig, key, entry->value.elem_type, entry->value.u.u64,
                   entry->value.u.i64)) {
            return 0;
        }
        if ((omc_jumbf_view_starts_with(key, unprotected_prefix)
             && key.size > unprotected_prefix_len
             && key.data[unprotected_prefix_len] == (omc_u8)'.')
            && (omc_jumbf_view_has_segment(key, "x5chain")
                || omc_jumbf_view_has_segment(key, "x5c")
                || omc_jumbf_view_has_segment(key, "33"))
            && entry->value.kind == OMC_VAL_BYTES) {
            if (out_sig->x5chain_count
                < (omc_u32)(sizeof(out_sig->x5chain_refs)
                            / sizeof(out_sig->x5chain_refs[0]))) {
                out_sig->x5chain_refs[out_sig->x5chain_count]
                    = entry->value.u.ref;
            }
            out_sig->x5chain_count += 1U;
            if (!out_sig->has_certificate_ref) {
                out_sig->certificate_ref = entry->value.u.ref;
                out_sig->has_certificate_ref = 1;
            }
        }
    }

    if (!out_sig->has_algorithm && out_sig->has_protected_ref) {
        omc_const_bytes protected_view;

        protected_view = omc_arena_view(&ctx->store->arena,
                                        out_sig->protected_ref);
        (void)omc_jumbf_cose_parse_protected_header(ctx, protected_view,
                                                    out_sig);
    }
    if (!out_sig->has_algorithm && out_sig->has_unprotected_alg_int) {
        if (omc_jumbf_cose_alg_name(out_sig->unprotected_alg_int,
                                    out_sig->algorithm,
                                    (omc_u32)sizeof(out_sig->algorithm),
                                    &out_sig->algorithm_len)) {
            out_sig->has_algorithm = 1;
        }
    }
    if (!out_sig->has_payload_ref && out_sig->payload_is_null) {
        omc_byte_ref refs[8];
        omc_u32 ref_count;

        ref_count = 0U;
        if (omc_jumbf_collect_detached_payload_refs(ctx, scan_limit, out_sig,
                                                    refs,
                                                    (omc_u32)(sizeof(refs)
                                                              / sizeof(refs[0])),
                                                    &ref_count)) {
            out_sig->payload_ref = refs[0];
            out_sig->has_payload_ref = 1;
            out_sig->payload_is_null = 0;
        }
    }
    return 1;
}

static omc_c2pa_verify_candidate*
omc_c2pa_add_or_get_verify_candidate(omc_c2pa_verify_candidate_list* list,
                                     const char* prefix, omc_u32 prefix_len)
{
    omc_u32 i;

    if (list == (omc_c2pa_verify_candidate_list*)0
        || prefix == (const char*)0) {
        return (omc_c2pa_verify_candidate*)0;
    }
    for (i = 0U; i < list->count; ++i) {
        if (list->items[i].prefix_len == prefix_len
            && memcmp(list->items[i].prefix, prefix, prefix_len) == 0) {
            return &list->items[i];
        }
    }
    if (list->count >= (omc_u32)(sizeof(list->items) / sizeof(list->items[0]))
        || prefix_len + 1U > (omc_u32)sizeof(list->items[0].prefix)) {
        return (omc_c2pa_verify_candidate*)0;
    }
    memset(&list->items[list->count], 0, sizeof(list->items[list->count]));
    memcpy(list->items[list->count].prefix, prefix, prefix_len);
    list->items[list->count].prefix[prefix_len] = '\0';
    list->items[list->count].prefix_len = prefix_len;
    list->count += 1U;
    return &list->items[list->count - 1U];
}

static int
omc_c2pa_add_verify_candidate_from_signature_prefix(
    omc_jumbf_ctx* ctx, omc_size scan_limit, const char* prefix,
    omc_u32 prefix_len, const char* source_box_prefix,
    omc_u32 source_box_prefix_len, omc_c2pa_verify_candidate_list* list)
{
    omc_jumbf_sig_proj sig;
    omc_c2pa_verify_candidate* candidate;
    omc_u32 i;
    omc_u32 detached_count;

    if (ctx == (omc_jumbf_ctx*)0 || prefix == (const char*)0
        || list == (omc_c2pa_verify_candidate_list*)0) {
        return 0;
    }
    if (!omc_jumbf_collect_signature_projection(ctx, scan_limit, prefix,
                                                prefix_len, &sig)) {
        return 0;
    }
    candidate = omc_c2pa_add_or_get_verify_candidate(list, prefix, prefix_len);
    if (candidate == (omc_c2pa_verify_candidate*)0) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }

    if (source_box_prefix != (const char*)0 && source_box_prefix_len != 0U
        && source_box_prefix_len + 1U
               <= (omc_u32)sizeof(candidate->source_box_prefix)) {
        memcpy(candidate->source_box_prefix, source_box_prefix,
               source_box_prefix_len);
        candidate->source_box_prefix[source_box_prefix_len] = '\0';
        candidate->source_box_prefix_len = source_box_prefix_len;
        candidate->has_source_box_prefix = 1;
    }
    if (sig.has_direct_claim_prefix
        && sig.direct_claim_prefix_len + 1U
               <= (omc_u32)sizeof(candidate->source_claim_prefix)) {
        memcpy(candidate->source_claim_prefix, sig.direct_claim_prefix,
               sig.direct_claim_prefix_len);
        candidate->source_claim_prefix[sig.direct_claim_prefix_len] = '\0';
        candidate->source_claim_prefix_len = sig.direct_claim_prefix_len;
        candidate->has_source_claim_prefix = 1;
    }
    if (sig.has_algorithm
        && sig.algorithm_len + 1U <= (omc_u32)sizeof(candidate->algorithm)) {
        memcpy(candidate->algorithm, sig.algorithm, sig.algorithm_len);
        candidate->algorithm[sig.algorithm_len] = '\0';
        candidate->algorithm_len = sig.algorithm_len;
        candidate->has_algorithm = 1;
    }
    if (sig.has_unprotected_alg_int) {
        candidate->cose_unprotected_alg_int = sig.unprotected_alg_int;
        candidate->has_cose_unprotected_alg_int = 1;
    }
    if (sig.has_protected_ref) {
        candidate->cose_protected_ref = sig.protected_ref;
        candidate->has_cose_protected_ref = 1;
    }
    if (sig.has_payload_ref) {
        candidate->cose_payload_ref = sig.payload_ref;
        candidate->has_cose_payload_ref = 1;
    }
    candidate->cose_payload_is_null = sig.payload_is_null;
    if (sig.has_signature_ref) {
        candidate->cose_signature_ref = sig.signature_ref;
        candidate->has_cose_signature_ref = 1;
        candidate->signature_ref = sig.signature_ref;
        candidate->has_signature_ref = 1;
    }
    if (sig.has_public_key_ref) {
        candidate->public_key_ref = sig.public_key_ref;
        candidate->has_public_key_ref = 1;
    }
    if (sig.has_certificate_ref) {
        candidate->certificate_ref = sig.certificate_ref;
        candidate->has_certificate_ref = 1;
    }
    candidate->certificate_chain_ref_count = 0U;
    for (i = 0U; i < sig.x5chain_count
                && i < (omc_u32)(sizeof(candidate->certificate_chain_refs)
                                 / sizeof(candidate->certificate_chain_refs[0]));
         ++i) {
        candidate->certificate_chain_refs[i] = sig.x5chain_refs[i];
        candidate->certificate_chain_ref_count += 1U;
    }

    detached_count = 0U;
    (void)omc_jumbf_collect_detached_payload_refs(
        ctx, scan_limit, &sig, candidate->detached_payload_refs,
        (omc_u32)(sizeof(candidate->detached_payload_refs)
                  / sizeof(candidate->detached_payload_refs[0])),
        &detached_count);
    candidate->detached_payload_ref_count = detached_count;
    return 1;
}

static int
omc_c2pa_collect_verify_candidates(
    omc_jumbf_ctx* ctx, omc_size scan_limit,
    omc_c2pa_verify_candidate_list* out_list, int* out_has_signatures)
{
    omc_size i;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0
        || out_list == (omc_c2pa_verify_candidate_list*)0
        || out_has_signatures == (int*)0) {
        return 0;
    }

    omc_c2pa_verify_candidate_list_init(out_list);
    *out_has_signatures = 0;

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char sig_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 sig_prefix_len;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key = omc_jumbf_entry_key_view(ctx->store, entry);
        if (omc_jumbf_view_has_segment(key, "signature")
            || omc_jumbf_view_has_segment(key, "signatures")) {
            *out_has_signatures = 1;
        }
        if (!omc_jumbf_find_indexed_segment_prefix(key, ".signatures[",
                                                   sig_prefix,
                                                   (omc_u32)sizeof(sig_prefix),
                                                   &sig_prefix_len)
            || omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                            i, ".signatures[", sig_prefix,
                                            sig_prefix_len)) {
            continue;
        }
        *out_has_signatures = 1;
        if (!omc_c2pa_add_verify_candidate_from_signature_prefix(
                ctx, scan_limit, sig_prefix, sig_prefix_len,
                (const char*)0, 0U, out_list)) {
            if (ctx->res.status != OMC_JUMBF_OK) {
                return 0;
            }
        }
    }

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_key;
        omc_const_bytes label_view;
        char box_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 box_prefix_len;
        omc_size j;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_FIELD
            || entry->value.kind != OMC_VAL_TEXT) {
            continue;
        }
        field_key = omc_jumbf_entry_key_view(ctx->store, entry);
        label_view = omc_jumbf_entry_value_view(ctx->store, entry);
        if (field_key.size < 6U
            || memcmp(field_key.data + field_key.size - 6U, ".label", 6U)
                   != 0
            || !omc_jumbf_ascii_icase_contains(label_view.data, label_view.size,
                                               "signature", 256U)) {
            continue;
        }
        if (field_key.size - 6U + 1U > (omc_u32)sizeof(box_prefix)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        memcpy(box_prefix, field_key.data, field_key.size - 6U);
        box_prefix[field_key.size - 6U] = '\0';
        box_prefix_len = (omc_u32)(field_key.size - 6U);
        *out_has_signatures = 1;

        for (j = 0U; j < scan_limit && j < ctx->store->entry_count; ++j) {
            const omc_entry* cbor_entry;
            omc_const_bytes key_view;
            char cbor_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 cbor_prefix_len;

            cbor_entry = &ctx->store->entries[j];
            if (cbor_entry->origin.block != ctx->block
                || cbor_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                continue;
            }
            key_view = omc_jumbf_entry_key_view(ctx->store, cbor_entry);
            if (key_view.size <= box_prefix_len
                || memcmp(key_view.data, box_prefix, box_prefix_len) != 0
                || key_view.data[box_prefix_len] != (omc_u8)'.'
                || !omc_jumbf_extract_cbor_root_prefix(
                       key_view, cbor_prefix, (omc_u32)sizeof(cbor_prefix),
                       &cbor_prefix_len)) {
                continue;
            }
            if (!omc_c2pa_add_verify_candidate_from_signature_prefix(
                    ctx, scan_limit, cbor_prefix, cbor_prefix_len, box_prefix,
                    box_prefix_len, out_list)) {
                if (ctx->res.status != OMC_JUMBF_OK) {
                    return 0;
                }
            }
            break;
        }
    }

    return 1;
}

static int
omc_jumbf_emit_signature_projection(omc_jumbf_ctx* ctx, omc_u32 index,
                                    const omc_jumbf_sig_proj* sig)
{
    if (ctx == (omc_jumbf_ctx*)0 || sig == (const omc_jumbf_sig_proj*)0) {
        return 0;
    }

    if (!omc_jumbf_emit_indexed_field_text(ctx, "c2pa.semantic.signature",
                                           index, "prefix", sig->prefix,
                                           sig->prefix_len)
        || !omc_jumbf_emit_indexed_field_u64(ctx, "c2pa.semantic.signature",
                                             index, "reference_key_hits",
                                             sig->reference_key_hits)
        || !omc_jumbf_emit_indexed_field_u64(ctx, "c2pa.semantic.signature",
                                             index, "key_hits",
                                             sig->key_hits)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "explicit_reference_index_hits", (omc_u64)sig->ref_index_count)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "explicit_reference_label_hits", (omc_u64)sig->ref_label_count)
        || !omc_jumbf_emit_indexed_field_u8(
               ctx, "c2pa.semantic.signature", index,
               "explicit_reference_present",
               (omc_u8)((sig->ref_index_count != 0U || sig->ref_label_count != 0U)
                            ? 1U
                            : 0U))) {
        return 0;
    }
    if (sig->has_algorithm
        && !omc_jumbf_emit_indexed_field_text(ctx, "c2pa.semantic.signature",
                                              index, "algorithm",
                                              sig->algorithm,
                                              sig->algorithm_len)) {
        return 0;
    }

    if (!omc_jumbf_emit_indexed_field_text(ctx, "c2pa.signature", index,
                                           "prefix", sig->prefix,
                                           sig->prefix_len)
        || !omc_jumbf_emit_indexed_field_u8(ctx, "c2pa.signature", index,
                                            "payload_is_null",
                                            (omc_u8)(sig->payload_is_null
                                                     ? 1U
                                                     : 0U))) {
        return 0;
    }
    if (sig->has_algorithm
        && !omc_jumbf_emit_indexed_field_text(ctx, "c2pa.signature", index,
                                              "algorithm", sig->algorithm,
                                              sig->algorithm_len)) {
        return 0;
    }
    if (sig->has_unprotected_alg_int
        && !omc_jumbf_emit_indexed_field_i64(
               ctx, "c2pa.signature", index, "cose_unprotected_alg",
               sig->unprotected_alg_int)) {
        return 0;
    }
    if (sig->has_protected_ref
        && !omc_jumbf_emit_indexed_field_bytes_ref(
               ctx, "c2pa.signature", index, "protected_bytes",
               sig->protected_ref)) {
        return 0;
    }
    if (sig->has_payload_ref
        && !omc_jumbf_emit_indexed_field_bytes_ref(ctx, "c2pa.signature",
                                                   index, "payload_bytes",
                                                   sig->payload_ref)) {
        return 0;
    }
    if (sig->has_signature_ref
        && !omc_jumbf_emit_indexed_field_bytes_ref(
               ctx, "c2pa.signature", index, "signature_bytes",
               sig->signature_ref)) {
        return 0;
    }
    if (sig->has_public_key_ref
        && !omc_jumbf_emit_indexed_field_bytes_ref(
               ctx, "c2pa.signature", index, "public_key_der",
               sig->public_key_ref)) {
        return 0;
    }
    if (sig->has_certificate_ref
        && !omc_jumbf_emit_indexed_field_bytes_ref(
               ctx, "c2pa.signature", index, "certificate_der",
               sig->certificate_ref)) {
        return 0;
    }
    if (sig->x5chain_count != 0U
        && !omc_jumbf_emit_indexed_field_u64(ctx, "c2pa.signature", index,
                                             "x5chain_count",
                                             (omc_u64)sig->x5chain_count)) {
        return 0;
    }
    return 1;
}

static int
omc_jumbf_emit_signature_ingredient_projection(
    omc_jumbf_ctx* ctx, omc_u32 index,
    const omc_jumbf_sig_link_proj* sig_link)
{
    char linked_claim_root[OMC_JUMBF_PATH_CAP];
    omc_u32 linked_claim_root_len;
    char signature_scope[OMC_JUMBF_PATH_CAP];
    omc_u32 signature_scope_len;
    omc_u64 linked_claim_count;
    omc_u64 linked_direct_claim_count;
    omc_u64 linked_cross_claim_count;
    omc_u32 i;

    if (ctx == (omc_jumbf_ctx*)0
        || sig_link == (const omc_jumbf_sig_link_proj*)0) {
        return 0;
    }

    linked_claim_count = sig_link->linked_claim_count;
    linked_direct_claim_count = sig_link->linked_direct_claim_count;
    linked_cross_claim_count = sig_link->linked_cross_claim_count;

    if (!omc_jumbf_emit_indexed_field_u64(
            ctx, "c2pa.semantic.signature", index, "linked_claim_count",
            sig_link->target_claim_count)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "cross_claim_link_count", sig_link->target_cross_claim_count)
        || !omc_jumbf_emit_indexed_field_u8(
               ctx, "c2pa.semantic.signature", index,
               "direct_claim_has_ingredients",
               (omc_u8)(sig_link->direct_claim_has_ingredients ? 1U : 0U))
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "linked_ingredient_claim_count", linked_claim_count)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "linked_direct_ingredient_claim_count",
               linked_direct_claim_count)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "linked_cross_ingredient_claim_count",
               linked_cross_claim_count)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "explicit_reference_resolved_claim_count",
               sig_link->explicit_reference_resolved_claim_count)
        || !omc_jumbf_emit_indexed_field_u8(
               ctx, "c2pa.semantic.signature", index,
               "explicit_reference_unresolved",
               (omc_u8)(sig_link->explicit_reference_unresolved ? 1U : 0U))
        || !omc_jumbf_emit_indexed_field_u8(
               ctx, "c2pa.semantic.signature", index,
               "explicit_reference_ambiguous",
               (omc_u8)(sig_link->explicit_reference_ambiguous ? 1U : 0U))) {
        return 0;
    }
    if (!omc_jumbf_build_indexed_scope("c2pa.semantic.signature", index,
                                       signature_scope,
                                       (omc_u32)sizeof(signature_scope),
                                       &signature_scope_len)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "linked_ingredient_title_count",
               sig_link->ingredient_summary.title_count)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "linked_ingredient_relationship_count",
               sig_link->ingredient_summary.relationship_count)
        || !omc_jumbf_emit_indexed_field_u64(
               ctx, "c2pa.semantic.signature", index,
               "linked_ingredient_thumbnail_url_count",
               sig_link->ingredient_summary.thumbnail_url_count)
        || !omc_jumbf_emit_named_counts(
               ctx, signature_scope, "linked_ingredient_relationship",
               &sig_link->ingredient_summary)) {
        return 0;
    }
    (void)signature_scope_len;
    if (!omc_jumbf_build_indexed_root("c2pa.semantic.signature", index,
                                      "linked_claim", linked_claim_root,
                                      (omc_u32)sizeof(linked_claim_root),
                                      &linked_claim_root_len)) {
        return 0;
    }
    (void)linked_claim_root_len;
    for (i = 0U; i < sig_link->target_claim_count; ++i) {
        if (!omc_jumbf_emit_indexed_field_text(
                ctx, linked_claim_root, i, "prefix",
                sig_link->target_claim_prefixes[i],
                sig_link->target_claim_lens[i])) {
            return 0;
        }
    }
    return 1;
}

static int
omc_jumbf_sem_proj_init(omc_jumbf_sem_proj* sem)
{
    if (sem == (omc_jumbf_sem_proj*)0) {
        return 0;
    }
    memset(sem, 0, sizeof(*sem));
    return 1;
}

static int
omc_jumbf_path_ends_with_segment_text(const char* path, omc_u32 path_len,
                                      const char* segment)
{
    omc_u32 seg_len;
    omc_u8 prev;

    if (path == (const char*)0 || segment == (const char*)0) {
        return 0;
    }
    seg_len = omc_jumbf_cstr_size(segment);
    if (seg_len == 0U || path_len < seg_len) {
        return 0;
    }
    if (memcmp(path + path_len - seg_len, segment, seg_len) != 0) {
        return 0;
    }
    if (path_len == seg_len) {
        return 1;
    }
    prev = (omc_u8)path[path_len - seg_len - 1U];
    return omc_jumbf_path_separator(prev);
}

static void
omc_jumbf_sem_note_key(omc_jumbf_sem_proj* sem, const char* path,
                       omc_u32 path_len)
{
    omc_const_bytes key;
    omc_u32 active_pos;

    if (sem == (omc_jumbf_sem_proj*)0 || path == (const char*)0) {
        return;
    }
    key = omc_jumbf_const_bytes(path, path_len);
    sem->cbor_key_count += 1U;
    if (omc_jumbf_view_has_segment(key, "manifest")
        || omc_jumbf_view_has_segment(key, "manifests")) {
        sem->has_manifest = 1;
    }
    if (omc_jumbf_view_has_segment(key, "claim")
        || omc_jumbf_view_has_segment(key, "claims")) {
        sem->has_claim = 1;
    }
    if (omc_jumbf_view_has_segment(key, "assertion")
        || omc_jumbf_view_has_segment(key, "assertions")) {
        sem->has_assertion = 1;
        sem->assertion_key_hits += 1U;
    }
    if (omc_jumbf_view_has_segment(key, "ingredient")
        || omc_jumbf_view_has_segment(key, "ingredients")) {
        sem->has_ingredient = 1;
        sem->ingredient_key_hits += 1U;
    }
    if (omc_jumbf_view_has_segment(key, "signature")
        || omc_jumbf_view_has_segment(key, "signatures")) {
        sem->has_signature = 1;
        sem->signature_key_hits += 1U;
    }
    if (!sem->active_manifest_present
        && omc_jumbf_view_find(key, ".manifests.active_manifest",
                               &active_pos)) {
        sem->active_manifest_present = 1;
        sem->active_manifest_count = 1U;
    }
}

static int
omc_jumbf_make_unprotected_prefix(const omc_jumbf_sig_proj* sig,
                                  char* out, omc_u32 out_cap,
                                  omc_u32* out_len)
{
    omc_u32 len;

    if (sig == (const omc_jumbf_sig_proj*)0 || out == (char*)0
        || out_len == (omc_u32*)0 || out_cap == 0U) {
        return 0;
    }
    len = 0U;
    out[0] = '\0';
    if (!omc_jumbf_append_mem(out, out_cap, &len, sig->prefix, sig->prefix_len)
        || !omc_jumbf_append_mem(out, out_cap, &len, "[1]", 3U)) {
        return 0;
    }
    *out_len = len;
    return 1;
}

static int
omc_jumbf_sem_sig_note_text(omc_jumbf_ctx* ctx, omc_jumbf_sig_proj* sig,
                            const char* path, omc_u32 path_len,
                            const char* text, omc_u32 text_len)
{
    omc_const_bytes key;
    char unprotected_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 unprotected_prefix_len;

    if (ctx == (omc_jumbf_ctx*)0 || sig == (omc_jumbf_sig_proj*)0
        || path == (const char*)0 || text == (const char*)0) {
        return 0;
    }
    if (!omc_jumbf_make_unprotected_prefix(sig, unprotected_prefix,
                                           (omc_u32)sizeof(unprotected_prefix),
                                           &unprotected_prefix_len)) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }

    key = omc_jumbf_const_bytes(path, path_len);
    if (!sig->has_algorithm
        && (omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                        "alg")
            || omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                           "algorithm")
            || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                           unprotected_prefix_len, "alg")
            || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                           unprotected_prefix_len,
                                           "algorithm"))
        && text_len != 0U
        && text_len < (omc_u32)sizeof(sig->algorithm)
        && omc_jumbf_bytes_all_ascii_printable((const omc_u8*)text, text_len)) {
        memcpy(sig->algorithm, text, text_len);
        sig->algorithm[text_len] = '\0';
        sig->algorithm_len = text_len;
        sig->has_algorithm = 1;
    }
    if (omc_jumbf_key_is_indexed_item(key, sig->prefix, sig->prefix_len, 2U)
        && text_len == 4U && memcmp(text, "null", 4U) == 0) {
        sig->payload_is_null = 1;
    } else if (omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                           "payload")
               && text_len == 4U && memcmp(text, "null", 4U) == 0) {
        sig->payload_is_null = 1;
    }
    return omc_jumbf_sig_proj_note_ref_text(sig, key, text, text_len);
}

static int
omc_jumbf_sem_sig_note_scalar(omc_jumbf_ctx* ctx, omc_jumbf_sig_proj* sig,
                              const char* path, omc_u32 path_len,
                              omc_elem_type elem_type, omc_u64 u64_value,
                              omc_s64 i64_value)
{
    omc_const_bytes key;
    char unprotected_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 unprotected_prefix_len;
    omc_s64 alg;

    if (ctx == (omc_jumbf_ctx*)0 || sig == (omc_jumbf_sig_proj*)0
        || path == (const char*)0) {
        return 0;
    }
    if (!omc_jumbf_make_unprotected_prefix(sig, unprotected_prefix,
                                           (omc_u32)sizeof(unprotected_prefix),
                                           &unprotected_prefix_len)) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }

    key = omc_jumbf_const_bytes(path, path_len);
    if (!sig->has_unprotected_alg_int
        && omc_jumbf_key_matches_field(key, unprotected_prefix,
                                       unprotected_prefix_len, "1")) {
        alg = 0;
        if (elem_type == OMC_ELEM_I64) {
            alg = i64_value;
        } else if (elem_type == OMC_ELEM_U64 || elem_type == OMC_ELEM_U32
                   || elem_type == OMC_ELEM_U16 || elem_type == OMC_ELEM_U8) {
            alg = (omc_s64)u64_value;
        } else {
            return 1;
        }

        sig->unprotected_alg_int = alg;
        sig->has_unprotected_alg_int = 1;
        if (!sig->has_algorithm
            && omc_jumbf_cose_alg_name(alg, sig->algorithm,
                                       (omc_u32)sizeof(sig->algorithm),
                                       &sig->algorithm_len)) {
            sig->has_algorithm = 1;
        }
    }
    return omc_jumbf_sig_proj_note_ref_scalar(sig, key, elem_type, u64_value,
                                              i64_value);
}

static int
omc_jumbf_sem_sig_note_bytes(omc_jumbf_ctx* ctx, omc_jumbf_sig_proj* sig,
                             const char* path, omc_u32 path_len,
                             const omc_u8* bytes, omc_u32 size)
{
    omc_const_bytes key;
    char unprotected_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 unprotected_prefix_len;

    if (ctx == (omc_jumbf_ctx*)0 || sig == (omc_jumbf_sig_proj*)0
        || path == (const char*)0) {
        return 0;
    }
    if (!omc_jumbf_make_unprotected_prefix(sig, unprotected_prefix,
                                           (omc_u32)sizeof(unprotected_prefix),
                                           &unprotected_prefix_len)) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }

    key = omc_jumbf_const_bytes(path, path_len);
    if (path_len == sig->prefix_len
        && memcmp(path, sig->prefix, sig->prefix_len) == 0
        && bytes != (const omc_u8*)0 && size != 0U) {
        (void)omc_jumbf_cose_parse_sign1_bytes(
            ctx, omc_jumbf_const_bytes(bytes, size), sig);
    } else if (omc_jumbf_key_is_indexed_item(key, sig->prefix,
                                             sig->prefix_len, 0U)
               && !sig->has_protected_ref) {
        sig->has_protected_ref = 1;
    } else if (omc_jumbf_key_is_indexed_item(key, sig->prefix,
                                             sig->prefix_len, 2U)
               && !sig->has_payload_ref) {
        sig->has_payload_ref = 1;
        sig->payload_is_null = 0;
    } else if (omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                           "payload")
               && !sig->has_payload_ref) {
        sig->has_payload_ref = 1;
        sig->payload_is_null = 0;
    } else if (omc_jumbf_key_is_indexed_item(key, sig->prefix,
                                             sig->prefix_len, 3U)
               && !sig->has_signature_ref) {
        sig->has_signature_ref = 1;
    } else if ((omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                            "signature")
                || omc_jumbf_key_matches_field(key, sig->prefix,
                                               sig->prefix_len, "sig"))
               && !sig->has_signature_ref) {
        sig->has_signature_ref = 1;
    } else if ((omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                            "public_key_der")
                || omc_jumbf_key_matches_field(key, sig->prefix,
                                               sig->prefix_len, "public_key")
                || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                               unprotected_prefix_len,
                                               "public_key_der")
                || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                               unprotected_prefix_len,
                                               "public_key"))
               && !sig->has_public_key_ref) {
        sig->has_public_key_ref = 1;
    } else if ((omc_jumbf_key_matches_field(key, sig->prefix, sig->prefix_len,
                                            "certificate_der")
                || omc_jumbf_key_matches_field(key, sig->prefix,
                                               sig->prefix_len, "certificate")
                || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                               unprotected_prefix_len,
                                               "certificate_der")
                || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                               unprotected_prefix_len,
                                               "certificate"))
               && !sig->has_certificate_ref) {
        sig->has_certificate_ref = 1;
    }

    if ((omc_jumbf_view_starts_with(key, unprotected_prefix)
         && key.size > unprotected_prefix_len
         && key.data[unprotected_prefix_len] == (omc_u8)'.')
        && (omc_jumbf_view_has_segment(key, "x5chain")
            || omc_jumbf_view_has_segment(key, "x5c")
            || omc_jumbf_view_has_segment(key, "33"))) {
        sig->x5chain_count += 1U;
        if (!sig->has_certificate_ref) {
            sig->has_certificate_ref = 1;
        }
    }
    return 1;
}

static int
omc_jumbf_cbor_read_small_text(const omc_u8* bytes, omc_size size,
                               omc_u64* pos, const omc_jumbf_cbor_head* head,
                               omc_u64 max_total, char* out, omc_u32 out_cap,
                               omc_u32* out_len);

static int
omc_jumbf_cbor_parse_key_raw(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                             omc_size size, omc_u64* pos, omc_u32 depth,
                             omc_u32 map_index, char* out_seg,
                             omc_u32 out_cap, omc_u32* out_len)
{
    omc_jumbf_cbor_head head;
    omc_u64 max_key;

    if (ctx == (omc_jumbf_ctx*)0 || out_seg == (char*)0
        || out_len == (omc_u32*)0) {
        return 0;
    }
    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }
    if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, &head)) {
        ctx->res.status = OMC_JUMBF_MALFORMED;
        return 0;
    }

    max_key = ctx->opts.limits.max_cbor_key_bytes;
    if (max_key >= (omc_u64)out_cap) {
        max_key = (omc_u64)out_cap - 1U;
    }

    if (head.major == 3U) {
        return omc_jumbf_cbor_read_key_text(ctx, bytes, size, pos, &head,
                                            max_key, out_seg, out_cap,
                                            out_len);
    }
    if (head.major == 0U) {
        omc_u32 len;

        len = 0U;
        out_seg[0] = '\0';
        if (!omc_jumbf_append_u64_dec(out_seg, out_cap, &len, head.arg)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        *out_len = len;
        return 1;
    }
    if (head.major == 1U) {
        omc_u32 len;

        len = 0U;
        out_seg[0] = '\0';
        if (!omc_jumbf_append_char(out_seg, out_cap, &len, 'n')
            || !omc_jumbf_append_u64_dec(out_seg, out_cap, &len, head.arg)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        *out_len = len;
        return 1;
    }
    if (head.major == 7U) {
        omc_u32 len;

        if (head.indefinite) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }
        len = 0U;
        out_seg[0] = '\0';
        if (head.addl == 20U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "false", 5U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (head.addl == 21U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "true", 4U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (head.addl == 22U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "null", 4U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (head.addl == 23U) {
            if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "undefined",
                                      9U)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            *out_len = len;
            return 1;
        }
        if (!omc_jumbf_append_mem(out_seg, out_cap, &len, "simple", 6U)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        *out_len = len;
        return 1;
    }

    if (!omc_jumbf_cbor_skip_from_head_raw(ctx, bytes, size, pos, depth + 1U,
                                           &head)) {
        return 0;
    }
    return omc_jumbf_make_synth_key_segment(
        map_index, omc_jumbf_cbor_major_suffix(head.major), out_seg, out_cap,
        out_len);
}

static int
omc_jumbf_measure_cbor_item(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                            omc_size size, omc_u64* pos, omc_u32 depth,
                            const char* path, omc_u32 path_len,
                            omc_jumbf_sem_proj* sem,
                            omc_jumbf_sig_proj* active_sig);

static int
omc_jumbf_measure_cbor_item_from_head(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                      omc_size size, omc_u64* pos,
                                      omc_u32 depth, const char* path,
                                      omc_u32 path_len,
                                      const omc_jumbf_cbor_head* head,
                                      omc_jumbf_sem_proj* sem,
                                      omc_jumbf_sig_proj* active_sig)
{
    omc_const_bytes key;
    omc_u64 count;

    if (ctx == (omc_jumbf_ctx*)0 || head == (const omc_jumbf_cbor_head*)0
        || sem == (omc_jumbf_sem_proj*)0) {
        return 0;
    }
    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }

    key = omc_jumbf_const_bytes(path, path_len);
    if (head->major == 0U) {
        omc_jumbf_sem_note_key(sem, path, path_len);
        if (active_sig != (omc_jumbf_sig_proj*)0) {
            active_sig->key_hits += 1U;
            if (!omc_jumbf_sem_sig_note_scalar(ctx, active_sig, path, path_len,
                                               OMC_ELEM_U64, head->arg,
                                               (omc_s64)0)) {
                return 0;
            }
        }
        return 1;
    }
    if (head->major == 1U) {
        omc_jumbf_sem_note_key(sem, path, path_len);
        if (active_sig != (omc_jumbf_sig_proj*)0) {
            omc_s64 i64_value;

            active_sig->key_hits += 1U;
            if (omc_jumbf_cbor_decode_i64(head, &i64_value)
                && !omc_jumbf_sem_sig_note_scalar(ctx, active_sig, path,
                                                  path_len, OMC_ELEM_I64, 0U,
                                                  i64_value)) {
                return 0;
            }
        }
        return 1;
    }
    if (head->major == 2U) {
        const omc_u8* payload;
        omc_u32 payload_size;

        omc_jumbf_sem_note_key(sem, path, path_len);
        if (active_sig != (omc_jumbf_sig_proj*)0) {
            active_sig->key_hits += 1U;
        }
        payload = (const omc_u8*)0;
        payload_size = 0U;
        if (!head->indefinite) {
            if (!omc_jumbf_cbor_read_definite_payload(bytes, size, pos, head,
                                                      ctx->opts.limits
                                                          .max_cbor_bytes_bytes,
                                                      &payload,
                                                      &payload_size)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }
        } else if (!omc_jumbf_cbor_skip_from_head_raw(ctx, bytes, size, pos,
                                                      depth + 1U, head)) {
            return 0;
        }
        if (active_sig != (omc_jumbf_sig_proj*)0
            && !omc_jumbf_sem_sig_note_bytes(ctx, active_sig, path, path_len,
                                             payload, payload_size)) {
            return 0;
        }
        return 1;
    }
    if (head->major == 3U) {
        int need_text;
        omc_u64 max_text;
        char text_buf[256];
        omc_u32 text_len;
        char unprotected_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 unprotected_prefix_len;

        omc_jumbf_sem_note_key(sem, path, path_len);
        if (active_sig != (omc_jumbf_sig_proj*)0) {
            active_sig->key_hits += 1U;
        }

        need_text = 0;
        max_text = 255U;
        if (!sem->have_claim_generator
            && omc_jumbf_view_has_segment(key, "claim_generator")) {
            need_text = 1;
            max_text = 255U;
        }
        if (active_sig != (omc_jumbf_sig_proj*)0) {
            if (!omc_jumbf_make_unprotected_prefix(active_sig,
                                                   unprotected_prefix,
                                                   (omc_u32)sizeof(
                                                       unprotected_prefix),
                                                   &unprotected_prefix_len)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            if (omc_jumbf_key_matches_field(key, active_sig->prefix,
                                            active_sig->prefix_len, "alg")
                || omc_jumbf_key_matches_field(key, active_sig->prefix,
                                               active_sig->prefix_len,
                                               "algorithm")
                || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                               unprotected_prefix_len, "alg")
                || omc_jumbf_key_matches_field(key, unprotected_prefix,
                                               unprotected_prefix_len,
                                               "algorithm")) {
                need_text = 1;
                max_text = 31U;
            }
            if (omc_jumbf_key_is_indexed_item(key, active_sig->prefix,
                                              active_sig->prefix_len, 2U)) {
                need_text = 1;
                if (max_text < 15U) {
                    max_text = 15U;
                }
            }
        }

        if (!need_text) {
            return omc_jumbf_cbor_skip_from_head_raw(ctx, bytes, size, pos,
                                                     depth + 1U, head);
        }

        if (!omc_jumbf_cbor_read_small_text(bytes, size, pos, head, max_text,
                                            text_buf,
                                            (omc_u32)sizeof(text_buf),
                                            &text_len)) {
            if (ctx->res.status == OMC_JUMBF_OK) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
            }
            return 0;
        }
        if (!sem->have_claim_generator
            && omc_jumbf_view_has_segment(key, "claim_generator")
            && text_len != 0U
            && omc_jumbf_bytes_all_ascii_printable((const omc_u8*)text_buf,
                                                   text_len)) {
            sem->have_claim_generator = 1;
        }
        if (active_sig != (omc_jumbf_sig_proj*)0
            && !omc_jumbf_sem_sig_note_text(ctx, active_sig, path, path_len,
                                            text_buf, text_len)) {
            return 0;
        }
        return 1;
    }
    if (head->major == 4U) {
        int array_is_manifests;
        int array_is_claims;
        int array_is_assertions;
        int array_is_signatures;

        array_is_manifests
            = omc_jumbf_path_ends_with_segment_text(path, path_len,
                                                    "manifests");
        array_is_claims
            = omc_jumbf_path_ends_with_segment_text(path, path_len, "claims");
        array_is_assertions
            = omc_jumbf_path_ends_with_segment_text(path, path_len,
                                                    "assertions");
        array_is_signatures
            = omc_jumbf_path_ends_with_segment_text(path, path_len,
                                                    "signatures");
        count = 0U;
        while (1) {
            char child_path[OMC_JUMBF_PATH_CAP];
            omc_u32 child_path_len;
            omc_jumbf_cbor_head child_head;

            if (head->indefinite) {
                if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                    return omc_jumbf_cbor_consume_break(bytes, size, pos);
                }
            } else if (count >= head->arg) {
                return 1;
            }

            if (!omc_jumbf_make_array_path(path, path_len, (omc_u32)count,
                                           child_path,
                                           (omc_u32)sizeof(child_path),
                                           &child_path_len)
                || !omc_jumbf_cbor_read_head_raw(bytes, size, pos,
                                                 &child_head)) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
                return 0;
            }

            if (array_is_manifests) {
                sem->manifest_count += 1U;
            }
            if (array_is_claims) {
                sem->claim_count += 1U;
            }
            if (array_is_assertions) {
                sem->assertion_count += 1U;
            }
            if (array_is_signatures) {
                omc_jumbf_sig_proj sig;
                omc_jumbf_sig_link_proj sig_link;
                omc_u32 tmp_pos;

                sem->signature_count += 1U;
                omc_jumbf_sig_proj_init(&sig);
                omc_jumbf_sig_link_proj_init(&sig_link);
                memcpy(sig.prefix, child_path, child_path_len);
                sig.prefix[child_path_len] = '\0';
                sig.prefix_len = child_path_len;
                sig.linked_to_claim
                    = omc_jumbf_view_find(
                          omc_jumbf_const_bytes(child_path, child_path_len),
                          ".claims[", &tmp_pos)
                      != 0;
                if (omc_jumbf_view_find(
                        omc_jumbf_const_bytes(child_path, child_path_len),
                        ".signatures[", &tmp_pos)
                    && tmp_pos != 0U
                    && tmp_pos + 1U
                           < (omc_u32)sizeof(sig.direct_claim_prefix)) {
                    memcpy(sig.direct_claim_prefix, child_path, tmp_pos);
                    sig.direct_claim_prefix[tmp_pos] = '\0';
                    sig.direct_claim_prefix_len = tmp_pos;
                    sig.has_direct_claim_prefix = 1;
                }
                if (sig.linked_to_claim) {
                    sem->signature_linked_count += 1U;
                }
                if (!omc_jumbf_measure_cbor_item_from_head(
                        ctx, bytes, size, pos, depth + 1U, child_path,
                        child_path_len, &child_head, sem, &sig)) {
                    return 0;
                }
                if (sig.has_direct_claim_prefix) {
                    if (!omc_jumbf_sig_link_add_target(
                            &sig_link, sig.direct_claim_prefix,
                            sig.direct_claim_prefix_len)) {
                        ctx->res.status = OMC_JUMBF_LIMIT;
                        return 0;
                    }
                    sig_link.explicit_reference_resolved_claim_count
                        = sig_link.target_claim_count;
                    if (!sig.linked_to_claim) {
                        sem->signature_linked_count += 1U;
                    }
                }
                if (!sig.has_payload_ref && sig.payload_is_null
                    && sig.has_direct_claim_prefix
                    && sig.ref_index_count == 0U
                    && sig.ref_label_count == 0U) {
                    sig.has_payload_ref = 1;
                    sig.payload_is_null = 0;
                }
                if (!omc_jumbf_emit_signature_projection(
                        ctx, sem->semantic_sig_index, &sig)
                    || !omc_jumbf_emit_signature_ingredient_projection(
                        ctx, sem->semantic_sig_index, &sig_link)) {
                    return 0;
                }
                sem->reference_key_hits += sig.reference_key_hits;
                sem->semantic_sig_index += 1U;
            } else {
                if (!omc_jumbf_measure_cbor_item_from_head(
                        ctx, bytes, size, pos, depth + 1U, child_path,
                        child_path_len, &child_head, sem, active_sig)) {
                    return 0;
                }
            }
            count += 1U;
        }
    }
    if (head->major == 5U) {
        omc_u64 map_index;

        map_index = 0U;
        while (1) {
            char seg[OMC_JUMBF_PATH_CAP];
            char child_path[OMC_JUMBF_PATH_CAP];
            omc_u32 seg_len;
            omc_u32 child_path_len;
            omc_jumbf_cbor_head value_head;

            if (head->indefinite) {
                if (omc_jumbf_cbor_peek_break(bytes, size, *pos)) {
                    return omc_jumbf_cbor_consume_break(bytes, size, pos);
                }
            } else if (map_index >= head->arg) {
                return 1;
            }

            if (!omc_jumbf_cbor_parse_key_raw(ctx, bytes, size, pos,
                                              depth + 1U, (omc_u32)map_index,
                                              seg, (omc_u32)sizeof(seg),
                                              &seg_len)
                || !omc_jumbf_make_path_with_segment(
                       path, path_len, seg, seg_len, child_path,
                       (omc_u32)sizeof(child_path), &child_path_len)
                || !omc_jumbf_cbor_read_head_raw(bytes, size, pos,
                                                 &value_head)) {
                if (ctx->res.status == OMC_JUMBF_OK) {
                    ctx->res.status = OMC_JUMBF_MALFORMED;
                }
                return 0;
            }
            if (!omc_jumbf_measure_cbor_item_from_head(
                    ctx, bytes, size, pos, depth + 1U, child_path,
                    child_path_len, &value_head, sem, active_sig)) {
                return 0;
            }
            map_index += 1U;
        }
    }
    if (head->major == 6U) {
        char tag_path[OMC_JUMBF_PATH_CAP];
        omc_u32 tag_path_len;

        if (!omc_jumbf_make_path_with_segment(path, path_len, "@tag", 4U,
                                              tag_path,
                                              (omc_u32)sizeof(tag_path),
                                              &tag_path_len)) {
            ctx->res.status = OMC_JUMBF_LIMIT;
            return 0;
        }
        omc_jumbf_sem_note_key(sem, tag_path, tag_path_len);
        if (active_sig != (omc_jumbf_sig_proj*)0) {
            active_sig->key_hits += 1U;
        }
        return omc_jumbf_measure_cbor_item(ctx, bytes, size, pos, depth + 1U,
                                           path, path_len, sem, active_sig);
    }
    if (head->major == 7U) {
        omc_jumbf_sem_note_key(sem, path, path_len);
        if (active_sig != (omc_jumbf_sig_proj*)0) {
            omc_elem_type elem_type;
            omc_u64 u64_value;
            omc_s64 i64_value;

            active_sig->key_hits += 1U;
            elem_type = OMC_ELEM_U8;
            u64_value = 0U;
            i64_value = 0;
            if (head->addl <= 24U) {
                u64_value = head->arg;
            } else if (head->addl == 20U) {
                u64_value = 0U;
            } else if (head->addl == 21U) {
                u64_value = 1U;
            } else if (head->addl == 25U || head->addl == 26U
                       || head->addl == 27U) {
                elem_type = OMC_ELEM_F64_BITS;
            }
            if (!omc_jumbf_sem_sig_note_scalar(ctx, active_sig, path, path_len,
                                               elem_type, u64_value,
                                               i64_value)) {
                return 0;
            }
        }
        return 1;
    }

    ctx->res.status = OMC_JUMBF_MALFORMED;
    return 0;
}

static int
omc_jumbf_measure_cbor_item(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                            omc_size size, omc_u64* pos, omc_u32 depth,
                            const char* path, omc_u32 path_len,
                            omc_jumbf_sem_proj* sem,
                            omc_jumbf_sig_proj* active_sig)
{
    omc_jumbf_cbor_head head;

    if (!omc_jumbf_cbor_depth_ok(ctx, depth)) {
        return 0;
    }
    if (!omc_jumbf_cbor_read_head_raw(bytes, size, pos, &head)) {
        ctx->res.status = OMC_JUMBF_MALFORMED;
        return 0;
    }
    return omc_jumbf_measure_cbor_item_from_head(ctx, bytes, size, pos, depth,
                                                 path, path_len, &head, sem,
                                                 active_sig);
}

static int
omc_jumbf_measure_cbor_payload(omc_jumbf_ctx* ctx, const omc_u8* payload,
                               omc_size payload_size, const char* prefix,
                               omc_u32 prefix_len, omc_jumbf_sem_proj* sem)
{
    omc_u64 pos;

    pos = 0U;
    while (pos < (omc_u64)payload_size) {
        if (!omc_jumbf_measure_cbor_item(ctx, payload, payload_size, &pos, 0U,
                                         prefix, prefix_len, sem,
                                         (omc_jumbf_sig_proj*)0)) {
            return 0;
        }
    }
    return 1;
}

static int
omc_jumbf_measure_boxes(omc_jumbf_ctx* ctx, const omc_u8* bytes, omc_size size,
                        omc_u64 begin, omc_u64 end, omc_u32 depth,
                        const char* parent_path, omc_u32 parent_len,
                        omc_jumbf_sem_proj* sem)
{
    omc_u64 off;
    omc_u32 child_index;

    if (ctx == (omc_jumbf_ctx*)0 || bytes == (const omc_u8*)0
        || sem == (omc_jumbf_sem_proj*)0 || begin > end
        || end > (omc_u64)size) {
        return 0;
    }
    if (ctx->opts.limits.max_box_depth != 0U
        && depth > ctx->opts.limits.max_box_depth) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }

    off = begin;
    child_index = 0U;
    while (off + 8U <= end) {
        omc_jumbf_box box;
        omc_u64 payload_off;
        omc_u64 payload_size;
        char box_path[OMC_JUMBF_PATH_CAP];
        omc_u32 box_path_len;

        if (!omc_jumbf_parse_box(bytes, size, off, end, &box)
            || !omc_jumbf_make_child_path(parent_path, parent_len, child_index,
                                          box_path,
                                          (omc_u32)sizeof(box_path),
                                          &box_path_len)) {
            ctx->res.status = OMC_JUMBF_MALFORMED;
            return 0;
        }

        payload_off = box.offset + box.header_size;
        payload_size = box.size - box.header_size;

        if (ctx->opts.decode_cbor && box.type == OMC_FOURCC('c', 'b', 'o', 'r')) {
            char cbor_path[OMC_JUMBF_PATH_CAP];
            omc_u32 cbor_path_len;

            if (!omc_jumbf_make_field_key(box_path, box_path_len, "cbor",
                                          cbor_path,
                                          (omc_u32)sizeof(cbor_path),
                                          &cbor_path_len)
                || !omc_jumbf_measure_cbor_payload(
                       ctx, bytes + (omc_size)payload_off, (omc_size)payload_size,
                       cbor_path, cbor_path_len, sem)) {
                if (ctx->res.status == OMC_JUMBF_OK) {
                    ctx->res.status = OMC_JUMBF_MALFORMED;
                }
                return 0;
            }
        }

        if (payload_size >= 8U
            && omc_jumbf_looks_like_bmff_sequence(bytes, size, payload_off,
                                                  payload_off + payload_size)) {
            if (!omc_jumbf_measure_boxes(ctx, bytes, size, payload_off,
                                         payload_off + payload_size,
                                         depth + 1U, box_path, box_path_len,
                                         sem)) {
                return 0;
            }
        }

        off += box.size;
        child_index += 1U;
        if (box.size == 0U) {
            break;
        }
    }
    return 1;
}

static int
omc_jumbf_project_c2pa_semantics_meas(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                      omc_size size)
{
    omc_jumbf_sem_proj sem;
    omc_jumbf_exp_ref_scope_stats exp_ref_stats;

    if (ctx == (omc_jumbf_ctx*)0 || bytes == (const omc_u8*)0) {
        return 0;
    }
    omc_jumbf_exp_ref_scope_stats_init(&exp_ref_stats);
    if (!omc_jumbf_sem_proj_init(&sem)
        || !omc_jumbf_measure_boxes(ctx, bytes, size, 0U, (omc_u64)size, 0U,
                                    (const char*)0, 0U, &sem)) {
        return 0;
    }

    if (sem.manifest_count == 0U && sem.has_manifest) {
        sem.manifest_count = 1U;
    }
    if (sem.claim_count == 0U && sem.has_claim) {
        sem.claim_count = 1U;
    }
    if (sem.signature_count == 0U && sem.has_signature) {
        sem.signature_count = 1U;
    }

    if ((sem.has_manifest || sem.has_claim || sem.has_assertion
         || sem.has_ingredient
         || sem.has_signature)
        && !omc_jumbf_emit_c2pa_marker(
               ctx, "cbor.semantic", omc_jumbf_cstr_size("cbor.semantic"))) {
        return 0;
    }
    if (sem.cbor_key_count == 0U && !ctx->c2pa_emitted) {
        omc_c2pa_profile_summary verify_summary;

        omc_c2pa_profile_summary_init(&verify_summary);
        verify_summary.available = 1;
        verify_summary.manifest_present = sem.has_manifest ? 1U : 0U;
        verify_summary.claim_present = sem.has_claim ? 1U : 0U;
        verify_summary.signature_present = sem.has_signature ? 1U : 0U;
        verify_summary.claim_count = sem.claim_count;
        verify_summary.signature_count = sem.signature_count;
        verify_summary.signature_linked = sem.signature_linked_count;
        verify_summary.signature_orphan
            = sem.signature_count >= sem.signature_linked_count
                  ? (sem.signature_count - sem.signature_linked_count)
                  : 0U;
        verify_summary.explicit_reference_signature_count
            = sem.explicit_reference_signature_count;
        verify_summary.explicit_reference_unresolved_signature_count
            = sem.explicit_reference_unresolved_signature_count;
        verify_summary.explicit_reference_ambiguous_signature_count
            = sem.explicit_reference_ambiguous_signature_count;
        return omc_jumbf_emit_c2pa_verify_scaffold(
            ctx, sem.signature_count != 0U || sem.has_signature, 0,
            &verify_summary);
    }

    if (!omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.cbor_key_count",
               omc_jumbf_cstr_size("c2pa.semantic.cbor_key_count"),
               sem.cbor_key_count)
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.active_manifest_present",
               omc_jumbf_cstr_size("c2pa.semantic.active_manifest_present"),
               (omc_u8)(sem.active_manifest_present ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.manifest_present",
               omc_jumbf_cstr_size("c2pa.semantic.manifest_present"),
               (omc_u8)(sem.has_manifest ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.claim_present",
               omc_jumbf_cstr_size("c2pa.semantic.claim_present"),
               (omc_u8)(sem.has_claim ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.assertion_present",
               omc_jumbf_cstr_size("c2pa.semantic.assertion_present"),
               (omc_u8)(sem.has_assertion ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.ingredient_present",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_present"),
               (omc_u8)(sem.has_ingredient ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.signature_present",
               omc_jumbf_cstr_size("c2pa.semantic.signature_present"),
               (omc_u8)(sem.has_signature ? 1U : 0U))
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.manifest_count",
               omc_jumbf_cstr_size("c2pa.semantic.manifest_count"),
               sem.manifest_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.active_manifest_count",
               omc_jumbf_cstr_size("c2pa.semantic.active_manifest_count"),
               sem.active_manifest_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.claim_count",
               omc_jumbf_cstr_size("c2pa.semantic.claim_count"),
               sem.claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.assertion_count",
               omc_jumbf_cstr_size("c2pa.semantic.assertion_count"),
               sem.assertion_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.assertion_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.assertion_key_hits"),
               sem.assertion_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_key_hits"),
               sem.ingredient_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.reference_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.reference_key_hits"),
               sem.reference_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.cross_claim_link_count",
               omc_jumbf_cstr_size("c2pa.semantic.cross_claim_link_count"),
               sem.cross_claim_link_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_count"),
               sem.ingredient_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_relationship_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_relationship_count"),
               sem.ingredient_relationship_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_thumbnail_url_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_thumbnail_url_count"),
               sem.ingredient_thumbnail_url_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.signature_key_hits"),
               sem.signature_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_count",
               omc_jumbf_cstr_size("c2pa.semantic.signature_count"),
               sem.signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_linked_count",
               omc_jumbf_cstr_size("c2pa.semantic.signature_linked_count"),
               sem.signature_linked_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_orphan_count",
               omc_jumbf_cstr_size("c2pa.semantic.signature_orphan_count"),
               sem.signature_count >= sem.signature_linked_count
                   ? (sem.signature_count - sem.signature_linked_count)
                   : 0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.explicit_reference_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_unresolved_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "explicit_reference_unresolved_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_ambiguous_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "explicit_reference_ambiguous_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_index_hits",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.explicit_reference_index_hits"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_label_hits",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.explicit_reference_label_hits"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_claim_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_claim_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_claim_with_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_claim_with_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_claim_referenced_by_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_claim_referenced_by_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_manifest_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_manifest_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_signature_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_claim_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_claim_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_direct_claim_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_direct_claim_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_claim_direct_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_claim_direct_source_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_cross_claim_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_cross_claim_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_claim_cross_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_claim_cross_source_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_claim_mixed_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_claim_mixed_source_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_direct_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_source_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_cross_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_source_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_mixed_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_mixed_source_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_signature_title_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_signature_title_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_relationship_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_relationship_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_thumbnail_url_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_thumbnail_url_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_direct_title_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_title_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_direct_relationship_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_relationship_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_direct_thumbnail_url_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_thumbnail_url_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_cross_title_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_title_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_cross_relationship_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_relationship_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_cross_thumbnail_url_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_thumbnail_url_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_explicit_reference_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_explicit_reference_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_explicit_reference_unresolved_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_explicit_reference_unresolved_signature_count"),
               0U)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_explicit_reference_ambiguous_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_explicit_reference_ambiguous_signature_count"),
               0U)) {
        return 0;
    }
    if (!omc_jumbf_emit_field_text(
            ctx, "c2pa.semantic.active_manifest.prefix",
            omc_jumbf_cstr_size("c2pa.semantic.active_manifest.prefix"),
            "", 0U)) {
        return 0;
    }
    if (!omc_jumbf_emit_named_counts(ctx, "c2pa.semantic",
                                     "ingredient_relationship",
                                     &sem.ingredient_summary)
        || !omc_jumbf_emit_named_counts(
               ctx, "c2pa.semantic",
               "ingredient_linked_signature_relationship",
               &sem.ingredient_summary)
        || !omc_jumbf_emit_exp_ref_scope_fields(
               ctx, "c2pa.semantic", &exp_ref_stats)) {
        return 0;
    }
    if (sem.have_claim_generator
        && !omc_jumbf_emit_field_text(
               ctx, "c2pa.semantic.claim_generator",
               omc_jumbf_cstr_size("c2pa.semantic.claim_generator"), "", 0U)) {
        return 0;
    }
    {
        omc_c2pa_profile_summary verify_summary;

        omc_c2pa_profile_summary_init(&verify_summary);
        verify_summary.available = 1;
        verify_summary.manifest_present = sem.has_manifest ? 1U : 0U;
        verify_summary.claim_present = sem.has_claim ? 1U : 0U;
        verify_summary.signature_present = sem.has_signature ? 1U : 0U;
        verify_summary.claim_count = sem.claim_count;
        verify_summary.signature_count = sem.signature_count;
        verify_summary.signature_linked = sem.signature_linked_count;
        verify_summary.signature_orphan
            = sem.signature_count >= sem.signature_linked_count
                  ? (sem.signature_count - sem.signature_linked_count)
                  : 0U;
        verify_summary.explicit_reference_signature_count
            = sem.explicit_reference_signature_count;
        verify_summary.explicit_reference_unresolved_signature_count
            = sem.explicit_reference_unresolved_signature_count;
        verify_summary.explicit_reference_ambiguous_signature_count
            = sem.explicit_reference_ambiguous_signature_count;
        return omc_jumbf_emit_c2pa_verify_scaffold(
            ctx, sem.signature_count != 0U || sem.has_signature,
            ctx->c2pa_emitted, &verify_summary);
    }
}

static int
omc_jumbf_project_c2pa_semantics(omc_jumbf_ctx* ctx, omc_size scan_limit)
{
    omc_size i;
    omc_u64 cbor_key_count;
    omc_u64 active_manifest_count;
    omc_u64 assertion_key_hits;
    omc_u64 assertion_count;
    omc_u64 ingredient_key_hits;
    omc_u64 reference_key_hits;
    omc_u64 ingredient_count;
    omc_u64 ingredient_relationship_count;
    omc_u64 ingredient_thumbnail_url_count;
    omc_u64 signature_key_hits;
    omc_u64 manifest_count;
    omc_u64 claim_count;
    omc_u64 signature_count;
    omc_u64 signature_linked_count;
    omc_u64 cross_claim_link_count;
    omc_u64 explicit_reference_signature_count;
    omc_u64 explicit_reference_unresolved_signature_count;
    omc_u64 explicit_reference_ambiguous_signature_count;
    omc_u64 explicit_reference_index_hits;
    omc_u64 explicit_reference_label_hits;
    omc_u64 ingredient_claim_count;
    omc_u64 ingredient_claim_with_signature_count;
    omc_u64 ingredient_claim_referenced_by_signature_count;
    omc_u64 ingredient_manifest_count;
    omc_u64 ingredient_signature_count;
    omc_u64 ingredient_linked_signature_count;
    omc_u64 ingredient_linked_claim_count;
    omc_u64 ingredient_linked_direct_claim_count;
    omc_u64 ingredient_linked_cross_claim_count;
    omc_u64 ingredient_linked_claim_mixed_source_count;
    omc_u64 ingredient_linked_signature_direct_source_count;
    omc_u64 ingredient_linked_signature_cross_source_count;
    omc_u64 ingredient_linked_signature_mixed_source_count;
    omc_u64 ingredient_explicit_reference_signature_count;
    omc_u64 ingredient_explicit_reference_unresolved_signature_count;
    omc_u64 ingredient_explicit_reference_ambiguous_signature_count;
    int has_manifest;
    int has_claim;
    int has_assertion;
    int has_ingredient;
    int has_signature;
    int active_manifest_present;
    int have_claim_generator;
    char active_manifest_prefix[OMC_JUMBF_PATH_CAP];
    omc_u32 active_manifest_prefix_len;
    char claim_generator[256];
    omc_u32 claim_generator_len;
    omc_u32 semantic_sig_index;
    omc_jumbf_ing_summary ingredient_summary;
    omc_jumbf_ing_summary linked_signature_summary;
    omc_jumbf_ing_summary linked_signature_direct_summary;
    omc_jumbf_ing_summary linked_signature_cross_summary;
    omc_jumbf_exp_ref_scope_stats ingredient_exp_ref_stats;

    if (ctx == (omc_jumbf_ctx*)0 || ctx->store == (omc_store*)0) {
        return 1;
    }

    cbor_key_count = 0U;
    active_manifest_count = 0U;
    assertion_key_hits = 0U;
    assertion_count = 0U;
    ingredient_key_hits = 0U;
    reference_key_hits = 0U;
    ingredient_count = 0U;
    ingredient_relationship_count = 0U;
    ingredient_thumbnail_url_count = 0U;
    omc_jumbf_ing_summary_init(&ingredient_summary);
    signature_key_hits = 0U;
    manifest_count = 0U;
    claim_count = 0U;
    signature_count = 0U;
    signature_linked_count = 0U;
    cross_claim_link_count = 0U;
    has_manifest = 0;
    has_claim = 0;
    has_assertion = 0;
    has_ingredient = 0;
    has_signature = 0;
    active_manifest_present = 0;
    have_claim_generator = 0;
    active_manifest_prefix_len = 0U;
    active_manifest_prefix[0] = '\0';
    claim_generator_len = 0U;
    semantic_sig_index = 0U;
    omc_jumbf_ing_summary_init(&linked_signature_summary);
    omc_jumbf_ing_summary_init(&linked_signature_direct_summary);
    omc_jumbf_ing_summary_init(&linked_signature_cross_summary);
    omc_jumbf_exp_ref_scope_stats_init(&ingredient_exp_ref_stats);
    explicit_reference_signature_count = 0U;
    explicit_reference_unresolved_signature_count = 0U;
    explicit_reference_ambiguous_signature_count = 0U;
    explicit_reference_index_hits = 0U;
    explicit_reference_label_hits = 0U;
    ingredient_claim_count = 0U;
    ingredient_claim_with_signature_count = 0U;
    ingredient_claim_referenced_by_signature_count = 0U;
    ingredient_manifest_count = 0U;
    ingredient_signature_count = 0U;
    ingredient_linked_signature_count = 0U;
    ingredient_linked_claim_count = 0U;
    ingredient_linked_direct_claim_count = 0U;
    ingredient_linked_cross_claim_count = 0U;
    ingredient_linked_claim_mixed_source_count = 0U;
    ingredient_linked_signature_direct_source_count = 0U;
    ingredient_linked_signature_cross_source_count = 0U;
    ingredient_linked_signature_mixed_source_count = 0U;
    ingredient_explicit_reference_signature_count = 0U;
    ingredient_explicit_reference_unresolved_signature_count = 0U;
    ingredient_explicit_reference_ambiguous_signature_count = 0U;

    for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key;
        char prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 prefix_len;
        char manifest_prefix[OMC_JUMBF_PATH_CAP];
        omc_u32 manifest_prefix_len;

        entry = &ctx->store->entries[i];
        if (entry->origin.block != ctx->block
            || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }

        key = omc_jumbf_entry_key_view(ctx->store, entry);
        cbor_key_count += 1U;
        if (omc_jumbf_view_has_segment(key, "manifest")
            || omc_jumbf_view_has_segment(key, "manifests")) {
            has_manifest = 1;
        }
        if (omc_jumbf_view_has_segment(key, "claim")
            || omc_jumbf_view_has_segment(key, "claims")) {
            has_claim = 1;
        }
        if (omc_jumbf_view_has_segment(key, "assertion")
            || omc_jumbf_view_has_segment(key, "assertions")) {
            has_assertion = 1;
            assertion_key_hits += 1U;
        }
        if (omc_jumbf_view_has_segment(key, "ingredient")
            || omc_jumbf_view_has_segment(key, "ingredients")) {
            has_ingredient = 1;
            ingredient_key_hits += 1U;
        }
        if (omc_jumbf_view_has_segment(key, "signature")
            || omc_jumbf_view_has_segment(key, "signatures")) {
            has_signature = 1;
            signature_key_hits += 1U;
        }
        if (omc_jumbf_extract_manifest_prefix_from_key(
                key, manifest_prefix, (omc_u32)sizeof(manifest_prefix),
                &manifest_prefix_len)
            && !omc_jumbf_manifest_prefix_seen_before(
                   ctx->store, ctx->block, scan_limit, i, manifest_prefix,
                   manifest_prefix_len)) {
            manifest_count += 1U;
            if (omc_jumbf_manifest_prefix_is_active(manifest_prefix,
                                                   manifest_prefix_len)) {
                active_manifest_present = 1;
                active_manifest_count += 1U;
                if (active_manifest_count == 1U) {
                    memcpy(active_manifest_prefix, manifest_prefix,
                           manifest_prefix_len);
                    active_manifest_prefix[manifest_prefix_len] = '\0';
                    active_manifest_prefix_len = manifest_prefix_len;
                } else {
                    active_manifest_prefix_len = 0U;
                    active_manifest_prefix[0] = '\0';
                }
            }
        }
        if (!have_claim_generator && entry->value.kind == OMC_VAL_TEXT
            && omc_jumbf_view_has_segment(key, "claim_generator")) {
            omc_const_bytes value_view;

            value_view = omc_jumbf_entry_value_view(ctx->store, entry);
            if (value_view.size < sizeof(claim_generator)
                && omc_jumbf_bytes_all_ascii_printable(
                       value_view.data, (omc_u32)value_view.size)) {
                memcpy(claim_generator, value_view.data, value_view.size);
                claim_generator[value_view.size] = '\0';
                claim_generator_len = (omc_u32)value_view.size;
                have_claim_generator = 1;
            }
        }

        if (omc_jumbf_find_indexed_segment_prefix(key, ".claims[", prefix,
                                                  (omc_u32)sizeof(prefix),
                                                  &prefix_len)
            && !omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                             i, ".claims[", prefix,
                                             prefix_len)) {
            claim_count += 1U;
        }
        if (omc_jumbf_find_indexed_segment_prefix(
                key, ".assertions[", prefix, (omc_u32)sizeof(prefix),
                &prefix_len)
            && !omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                             i, ".assertions[", prefix,
                                             prefix_len)) {
            assertion_count += 1U;
        }
        if (omc_jumbf_find_indexed_segment_prefix(key, ".ingredients[", prefix,
                                                  (omc_u32)sizeof(prefix),
                                                  &prefix_len)
            && !omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                             i, ".ingredients[", prefix,
                                             prefix_len)) {
            omc_jumbf_ing_proj ingredient;

            ingredient_count += 1U;
            if (!omc_jumbf_collect_ingredient_projection(ctx, scan_limit, prefix,
                                                         prefix_len,
                                                         &ingredient)) {
                return 0;
            }
            if (ingredient.has_relationship) {
                ingredient_relationship_count += 1U;
                omc_jumbf_ing_summary_note_relationship(
                    &ingredient_summary, ingredient.relationship,
                    ingredient.relationship_len);
            }
            if (ingredient.has_thumbnail_url) {
                ingredient_thumbnail_url_count += 1U;
            }
        }
        if (omc_jumbf_find_indexed_segment_prefix(key, ".signatures[", prefix,
                                                  (omc_u32)sizeof(prefix),
                                                  &prefix_len)
            && !omc_jumbf_prefix_seen_before(ctx->store, ctx->block, scan_limit,
                                             i, ".signatures[", prefix,
                                             prefix_len)) {
            omc_jumbf_sig_proj sig;
            omc_jumbf_sig_link_proj sig_link;

            signature_count += 1U;
            if (!omc_jumbf_collect_signature_projection(ctx, scan_limit, prefix,
                                                        prefix_len,
                                                        &sig)
                || !omc_jumbf_collect_signature_ingredient_link(
                       ctx, scan_limit, &sig, &sig_link)
                || !omc_jumbf_emit_signature_projection(ctx, semantic_sig_index,
                                                        &sig)
                || !omc_jumbf_emit_signature_ingredient_projection(
                       ctx, semantic_sig_index, &sig_link)) {
                return 0;
            }
            if (sig_link.target_claim_count != 0U) {
                signature_linked_count += 1U;
            }
            if (sig_link.explicit_reference_present) {
                explicit_reference_signature_count += 1U;
                explicit_reference_index_hits
                    += sig_link.explicit_reference_index_hits;
                explicit_reference_label_hits
                    += sig_link.explicit_reference_label_hits;
                if (sig_link.explicit_reference_unresolved) {
                    explicit_reference_unresolved_signature_count += 1U;
                } else if (sig_link.explicit_reference_ambiguous) {
                    explicit_reference_ambiguous_signature_count += 1U;
                }
            }
            cross_claim_link_count += sig_link.target_cross_claim_count;
            reference_key_hits += sig.reference_key_hits;
            semantic_sig_index += 1U;
        }
    }

    if (manifest_count == 0U && has_manifest) {
        manifest_count = 1U;
    }
    if (claim_count == 0U && has_claim) {
        claim_count = 1U;
    }
    if (signature_count == 0U && has_signature) {
        signature_count = 1U;
    }

    {
        omc_size claim_i;

        for (claim_i = 0U; claim_i < scan_limit
                          && claim_i < ctx->store->entry_count;
             ++claim_i) {
            const omc_entry* claim_entry;
            omc_const_bytes claim_key;
            char claim_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 claim_prefix_len;
            omc_jumbf_ing_summary claim_summary;
            omc_u64 nested_signature_count;
            omc_u64 referenced_by_signature_count;
            omc_u64 linked_signature_count_for_claim;
            omc_u64 linked_direct_signature_count_for_claim;
            omc_u64 linked_cross_signature_count_for_claim;
            omc_u64 linked_explicit_reference_direct_signature_count_for_claim;
            omc_u64 linked_explicit_reference_cross_signature_count_for_claim;
            omc_jumbf_ing_summary linked_claim_summary;
            omc_jumbf_ing_summary linked_claim_direct_summary;
            omc_jumbf_ing_summary linked_claim_cross_summary;
            omc_jumbf_claim_signature_stats claim_sig_stats;
            omc_jumbf_claim_link_stats claim_link_stats;

            claim_entry = &ctx->store->entries[claim_i];
            if (claim_entry->origin.block != ctx->block
                || claim_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                continue;
            }
            claim_key = omc_jumbf_entry_key_view(ctx->store, claim_entry);
            if (!omc_jumbf_find_indexed_segment_prefix(
                    claim_key, ".claims[", claim_prefix,
                    (omc_u32)sizeof(claim_prefix), &claim_prefix_len)
                || omc_jumbf_prefix_seen_before(
                       ctx->store, ctx->block, scan_limit, claim_i,
                       ".claims[", claim_prefix, claim_prefix_len)) {
                continue;
            }
            if (!omc_jumbf_collect_claim_ingredient_summary(
                    ctx, scan_limit, claim_prefix, claim_prefix_len,
                    &claim_summary)
                || !omc_jumbf_collect_claim_signature_stats(
                       ctx, scan_limit, claim_prefix, claim_prefix_len,
                       &claim_sig_stats)
                || !omc_jumbf_collect_claim_signature_topology(
                       ctx, scan_limit, claim_prefix, claim_prefix_len,
                       &claim_link_stats)) {
                return 0;
            }
            nested_signature_count = claim_link_stats.nested_signature_count;
            referenced_by_signature_count
                = claim_link_stats.referenced_by_signature_count;
            linked_signature_count_for_claim
                = claim_link_stats.linked_signature_count;
            linked_direct_signature_count_for_claim
                = claim_link_stats.linked_direct_signature_count;
            linked_cross_signature_count_for_claim
                = claim_link_stats.linked_cross_signature_count;
            linked_explicit_reference_direct_signature_count_for_claim
                = claim_link_stats
                      .linked_explicit_reference_direct_signature_count;
            linked_explicit_reference_cross_signature_count_for_claim
                = claim_link_stats
                      .linked_explicit_reference_cross_signature_count;
            linked_claim_summary = claim_link_stats.linked_summary;
            linked_claim_direct_summary
                = claim_link_stats.linked_direct_summary;
            linked_claim_cross_summary
                = claim_link_stats.linked_cross_summary;
            (void)linked_explicit_reference_direct_signature_count_for_claim;
            (void)linked_explicit_reference_cross_signature_count_for_claim;
            if (claim_summary.ingredient_count == 0U) {
                continue;
            }

            ingredient_claim_count += 1U;
            ingredient_signature_count += nested_signature_count;
            ingredient_explicit_reference_unresolved_signature_count
                += claim_sig_stats
                       .explicit_reference_unresolved_signature_count;
            ingredient_explicit_reference_ambiguous_signature_count
                += claim_sig_stats
                       .explicit_reference_ambiguous_signature_count;
            if (nested_signature_count != 0U) {
                ingredient_claim_with_signature_count += 1U;
            }
            if (referenced_by_signature_count != 0U) {
                ingredient_claim_referenced_by_signature_count += 1U;
            }
            if (linked_signature_count_for_claim != 0U) {
                ingredient_linked_claim_count += 1U;
                ingredient_linked_signature_count
                    += linked_signature_count_for_claim;
                omc_jumbf_ing_summary_merge(&linked_signature_summary,
                                            &linked_claim_summary);
                ingredient_linked_signature_direct_source_count
                    += linked_direct_signature_count_for_claim;
                ingredient_linked_signature_cross_source_count
                    += linked_cross_signature_count_for_claim;
                if (linked_direct_signature_count_for_claim != 0U
                    && linked_cross_signature_count_for_claim != 0U) {
                    ingredient_linked_signature_mixed_source_count += 1U;
                }
                ingredient_explicit_reference_signature_count
                    += claim_link_stats
                           .linked_explicit_reference_signature_count;
                omc_jumbf_exp_ref_scope_stats_note_claim(
                    &ingredient_exp_ref_stats, &claim_link_stats);
                if (linked_direct_signature_count_for_claim != 0U) {
                    ingredient_linked_direct_claim_count += 1U;
                    omc_jumbf_ing_summary_merge(
                        &linked_signature_direct_summary,
                        &linked_claim_direct_summary);
                }
                if (linked_cross_signature_count_for_claim != 0U) {
                    ingredient_linked_cross_claim_count += 1U;
                    omc_jumbf_ing_summary_merge(
                        &linked_signature_cross_summary,
                        &linked_claim_cross_summary);
                }
                if (linked_direct_signature_count_for_claim != 0U
                    && linked_cross_signature_count_for_claim != 0U) {
                    ingredient_linked_claim_mixed_source_count += 1U;
                }
            }
        }
    }
    {
        omc_size manifest_i;

        for (manifest_i = 0U; manifest_i < scan_limit
                             && manifest_i < ctx->store->entry_count;
             ++manifest_i) {
            const omc_entry* manifest_entry;
            omc_const_bytes manifest_key;
            char manifest_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 manifest_prefix_len;
            omc_size claim_i;
            int manifest_has_ingredient;

            manifest_entry = &ctx->store->entries[manifest_i];
            if (manifest_entry->origin.block != ctx->block
                || manifest_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                continue;
            }
            manifest_key = omc_jumbf_entry_key_view(ctx->store, manifest_entry);
            if (!omc_jumbf_extract_manifest_prefix_from_key(
                    manifest_key, manifest_prefix,
                    (omc_u32)sizeof(manifest_prefix), &manifest_prefix_len)
                || omc_jumbf_manifest_prefix_seen_before(
                       ctx->store, ctx->block, scan_limit, manifest_i,
                       manifest_prefix, manifest_prefix_len)) {
                continue;
            }

            manifest_has_ingredient = 0;
            for (claim_i = 0U; claim_i < scan_limit
                              && claim_i < ctx->store->entry_count;
                 ++claim_i) {
                const omc_entry* claim_entry;
                omc_const_bytes claim_key;
                char claim_prefix[OMC_JUMBF_PATH_CAP];
                omc_u32 claim_prefix_len;
                char claim_manifest_prefix[OMC_JUMBF_PATH_CAP];
                omc_u32 claim_manifest_prefix_len;
                omc_jumbf_ing_summary claim_summary;

                claim_entry = &ctx->store->entries[claim_i];
                if (claim_entry->origin.block != ctx->block
                    || claim_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                    continue;
                }
                claim_key = omc_jumbf_entry_key_view(ctx->store, claim_entry);
                if (!omc_jumbf_find_indexed_segment_prefix(
                        claim_key, ".claims[", claim_prefix,
                        (omc_u32)sizeof(claim_prefix), &claim_prefix_len)
                    || omc_jumbf_prefix_seen_before(
                           ctx->store, ctx->block, scan_limit, claim_i,
                           ".claims[", claim_prefix, claim_prefix_len)
                    || !omc_jumbf_extract_manifest_prefix_from_claim_prefix(
                           claim_prefix, claim_prefix_len,
                           claim_manifest_prefix,
                           (omc_u32)sizeof(claim_manifest_prefix),
                           &claim_manifest_prefix_len)
                    || claim_manifest_prefix_len != manifest_prefix_len
                    || memcmp(claim_manifest_prefix, manifest_prefix,
                              manifest_prefix_len)
                           != 0
                    || !omc_jumbf_collect_claim_ingredient_summary(
                           ctx, scan_limit, claim_prefix, claim_prefix_len,
                           &claim_summary)) {
                    if (ctx->res.status != OMC_JUMBF_OK) {
                        return 0;
                    }
                    continue;
                }
                if (claim_summary.ingredient_count != 0U) {
                    manifest_has_ingredient = 1;
                    break;
                }
            }
            if (manifest_has_ingredient) {
                ingredient_manifest_count += 1U;
            }
        }
    }

    if ((has_manifest || has_claim || has_assertion || has_ingredient
         || has_signature)
        && !omc_jumbf_emit_c2pa_marker(
               ctx, "cbor.semantic", omc_jumbf_cstr_size("cbor.semantic"))) {
        return 0;
    }
    if (cbor_key_count == 0U && !ctx->c2pa_emitted) {
        omc_c2pa_profile_summary verify_summary;

        omc_c2pa_profile_summary_init(&verify_summary);
        verify_summary.available = 1;
        verify_summary.manifest_present = has_manifest ? 1U : 0U;
        verify_summary.claim_present = has_claim ? 1U : 0U;
        verify_summary.signature_present = has_signature ? 1U : 0U;
        verify_summary.claim_count = claim_count;
        verify_summary.signature_count = signature_count;
        verify_summary.signature_linked = signature_linked_count;
        verify_summary.signature_orphan
            = signature_count >= signature_linked_count
                  ? (signature_count - signature_linked_count)
                  : 0U;
        verify_summary.explicit_reference_signature_count
            = explicit_reference_signature_count;
        verify_summary.explicit_reference_unresolved_signature_count
            = explicit_reference_unresolved_signature_count;
        verify_summary.explicit_reference_ambiguous_signature_count
            = explicit_reference_ambiguous_signature_count;
        return omc_jumbf_emit_c2pa_verify_scaffold(
            ctx, signature_count != 0U || has_signature, 0,
            &verify_summary);
    }

    if (!omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.cbor_key_count",
               omc_jumbf_cstr_size("c2pa.semantic.cbor_key_count"),
               cbor_key_count)
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.active_manifest_present",
               omc_jumbf_cstr_size("c2pa.semantic.active_manifest_present"),
               (omc_u8)(active_manifest_present ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.manifest_present",
               omc_jumbf_cstr_size("c2pa.semantic.manifest_present"),
               (omc_u8)(has_manifest ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.claim_present",
               omc_jumbf_cstr_size("c2pa.semantic.claim_present"),
               (omc_u8)(has_claim ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.assertion_present",
               omc_jumbf_cstr_size("c2pa.semantic.assertion_present"),
               (omc_u8)(has_assertion ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.ingredient_present",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_present"),
               (omc_u8)(has_ingredient ? 1U : 0U))
        || !omc_jumbf_emit_field_u8(
               ctx, "c2pa.semantic.signature_present",
               omc_jumbf_cstr_size("c2pa.semantic.signature_present"),
               (omc_u8)(has_signature ? 1U : 0U))
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.manifest_count",
               omc_jumbf_cstr_size("c2pa.semantic.manifest_count"),
               manifest_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.active_manifest_count",
               omc_jumbf_cstr_size("c2pa.semantic.active_manifest_count"),
               active_manifest_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.claim_count",
               omc_jumbf_cstr_size("c2pa.semantic.claim_count"), claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.assertion_count",
               omc_jumbf_cstr_size("c2pa.semantic.assertion_count"),
               assertion_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.assertion_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.assertion_key_hits"),
               assertion_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_key_hits"),
               ingredient_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.reference_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.reference_key_hits"),
               reference_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.cross_claim_link_count",
               omc_jumbf_cstr_size("c2pa.semantic.cross_claim_link_count"),
               cross_claim_link_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_count"),
               ingredient_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_relationship_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_relationship_count"),
               ingredient_relationship_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_thumbnail_url_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_thumbnail_url_count"),
               ingredient_thumbnail_url_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_claim_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_claim_count"),
               ingredient_claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_claim_with_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_claim_with_signature_count"),
               ingredient_claim_with_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_claim_referenced_by_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_claim_referenced_by_signature_count"),
               ingredient_claim_referenced_by_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_manifest_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_manifest_count"),
               ingredient_manifest_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_key_hits",
               omc_jumbf_cstr_size("c2pa.semantic.signature_key_hits"),
               signature_key_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_count",
               omc_jumbf_cstr_size("c2pa.semantic.signature_count"),
               signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_linked_count",
               omc_jumbf_cstr_size("c2pa.semantic.signature_linked_count"),
               signature_linked_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.signature_orphan_count",
               omc_jumbf_cstr_size("c2pa.semantic.signature_orphan_count"),
               signature_count >= signature_linked_count
                   ? (signature_count - signature_linked_count)
                   : 0U)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.explicit_reference_signature_count"),
               explicit_reference_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_unresolved_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "explicit_reference_unresolved_signature_count"),
               explicit_reference_unresolved_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_ambiguous_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "explicit_reference_ambiguous_signature_count"),
               explicit_reference_ambiguous_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_index_hits",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.explicit_reference_index_hits"),
               explicit_reference_index_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.explicit_reference_label_hits",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.explicit_reference_label_hits"),
               explicit_reference_label_hits)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_signature_count",
               omc_jumbf_cstr_size("c2pa.semantic.ingredient_signature_count"),
               ingredient_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_signature_count"),
               ingredient_linked_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_claim_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_claim_count"),
               ingredient_linked_claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_direct_claim_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_direct_claim_count"),
               ingredient_linked_direct_claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_claim_direct_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_claim_direct_source_count"),
               ingredient_linked_direct_claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_cross_claim_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_cross_claim_count"),
               ingredient_linked_cross_claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_claim_cross_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_claim_cross_source_count"),
               ingredient_linked_cross_claim_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_claim_mixed_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_claim_mixed_source_count"),
               ingredient_linked_claim_mixed_source_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_direct_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_source_count"),
               ingredient_linked_signature_direct_source_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_cross_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_source_count"),
               ingredient_linked_signature_cross_source_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_mixed_source_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_mixed_source_count"),
               ingredient_linked_signature_mixed_source_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_linked_signature_title_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_linked_signature_title_count"),
               linked_signature_summary.title_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_relationship_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_relationship_count"),
               linked_signature_summary.relationship_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_thumbnail_url_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_thumbnail_url_count"),
               linked_signature_summary.thumbnail_url_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_direct_title_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_title_count"),
               linked_signature_direct_summary.title_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_direct_relationship_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_relationship_count"),
               linked_signature_direct_summary.relationship_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_direct_thumbnail_url_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_direct_thumbnail_url_count"),
               linked_signature_direct_summary.thumbnail_url_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_linked_signature_cross_title_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_title_count"),
               linked_signature_cross_summary.title_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_cross_relationship_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_relationship_count"),
               linked_signature_cross_summary.relationship_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic."
               "ingredient_linked_signature_cross_thumbnail_url_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_linked_signature_cross_thumbnail_url_count"),
               linked_signature_cross_summary.thumbnail_url_count)
        || !omc_jumbf_emit_field_u64(
               ctx, "c2pa.semantic.ingredient_explicit_reference_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic.ingredient_explicit_reference_signature_count"),
               ingredient_explicit_reference_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_explicit_reference_unresolved_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_explicit_reference_unresolved_signature_count"),
               ingredient_explicit_reference_unresolved_signature_count)
        || !omc_jumbf_emit_field_u64(
               ctx,
               "c2pa.semantic.ingredient_explicit_reference_ambiguous_signature_count",
               omc_jumbf_cstr_size(
                   "c2pa.semantic."
                   "ingredient_explicit_reference_ambiguous_signature_count"),
               ingredient_explicit_reference_ambiguous_signature_count)) {
        return 0;
    }
    if (!omc_jumbf_emit_field_text(
            ctx, "c2pa.semantic.active_manifest.prefix",
            omc_jumbf_cstr_size("c2pa.semantic.active_manifest.prefix"),
            active_manifest_prefix, active_manifest_prefix_len)) {
        return 0;
    }
    if (!omc_jumbf_emit_named_counts(ctx, "c2pa.semantic",
                                     "ingredient_relationship",
                                     &ingredient_summary)) {
        return 0;
    }
    if (!omc_jumbf_emit_named_counts(
            ctx, "c2pa.semantic", "ingredient_linked_signature_relationship",
            &linked_signature_summary)) {
        return 0;
    }
    if (!omc_jumbf_emit_exp_ref_scope_fields(
            ctx, "c2pa.semantic", &ingredient_exp_ref_stats)) {
        return 0;
    }
    {
        omc_u32 claim_sem_index;

        claim_sem_index = 0U;
        for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
            const omc_entry* entry;
            omc_const_bytes key;
            char claim_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 claim_prefix_len;
            omc_jumbf_ing_summary claim_summary;
            omc_u64 nested_signature_count;
            omc_u64 referenced_by_signature_count;
            omc_u64 linked_signature_count_for_claim;
            omc_u64 linked_direct_signature_count_for_claim;
            omc_u64 linked_cross_signature_count_for_claim;
            omc_u64 linked_explicit_reference_signature_count_for_claim;
            omc_u64 linked_explicit_reference_direct_signature_count_for_claim;
            omc_u64 linked_explicit_reference_cross_signature_count_for_claim;
            omc_jumbf_ing_summary linked_claim_summary;
            omc_jumbf_ing_summary linked_claim_direct_summary;
            omc_jumbf_ing_summary linked_claim_cross_summary;
            omc_u64 claim_assertion_count;
            omc_u64 claim_signature_count;
            omc_u64 claim_signature_key_hits;
            int claim_has_claim_generator;
            char claim_generator_text[256];
            omc_u32 claim_generator_text_len;
            omc_u32 claim_assertion_index;
            omc_u32 claim_ingredient_index;
            omc_u32 claim_signature_index;
            omc_size j;
            char claim_scope[OMC_JUMBF_PATH_CAP];
            omc_u32 claim_scope_len;
            omc_jumbf_claim_link_stats claim_link_stats;

            entry = &ctx->store->entries[i];
            if (entry->origin.block != ctx->block
                || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                continue;
            }
            key = omc_jumbf_entry_key_view(ctx->store, entry);
            if (!omc_jumbf_find_indexed_segment_prefix(key, ".claims[",
                                                       claim_prefix,
                                                       (omc_u32)sizeof(claim_prefix),
                                                       &claim_prefix_len)
                || omc_jumbf_prefix_seen_before(ctx->store, ctx->block,
                                                scan_limit, i, ".claims[",
                                                claim_prefix,
                                                claim_prefix_len)) {
                continue;
            }

            if (!omc_jumbf_collect_claim_ingredient_summary(
                    ctx, scan_limit, claim_prefix, claim_prefix_len,
                    &claim_summary)
                || !omc_jumbf_collect_claim_signature_topology(
                       ctx, scan_limit, claim_prefix, claim_prefix_len,
                       &claim_link_stats)
                || !omc_jumbf_build_indexed_scope(
                       "c2pa.semantic.claim", claim_sem_index, claim_scope,
                       (omc_u32)sizeof(claim_scope), &claim_scope_len)) {
                return 0;
            }
            nested_signature_count = claim_link_stats.nested_signature_count;
            referenced_by_signature_count
                = claim_link_stats.referenced_by_signature_count;
            linked_signature_count_for_claim
                = claim_link_stats.linked_signature_count;
            linked_direct_signature_count_for_claim
                = claim_link_stats.linked_direct_signature_count;
            linked_cross_signature_count_for_claim
                = claim_link_stats.linked_cross_signature_count;
            linked_explicit_reference_signature_count_for_claim
                = claim_link_stats.linked_explicit_reference_signature_count;
            linked_explicit_reference_direct_signature_count_for_claim
                = claim_link_stats
                      .linked_explicit_reference_direct_signature_count;
            linked_explicit_reference_cross_signature_count_for_claim
                = claim_link_stats
                      .linked_explicit_reference_cross_signature_count;
            linked_claim_summary = claim_link_stats.linked_summary;
            linked_claim_direct_summary
                = claim_link_stats.linked_direct_summary;
            linked_claim_cross_summary
                = claim_link_stats.linked_cross_summary;
            (void)nested_signature_count;
            (void)linked_claim_direct_summary;
            (void)linked_claim_cross_summary;
            claim_assertion_count = 0U;
            claim_signature_count = 0U;
            claim_signature_key_hits = 0U;
            claim_has_claim_generator = 0;
            claim_generator_text_len = 0U;
            claim_assertion_index = 0U;
            claim_ingredient_index = 0U;
            claim_signature_index = 0U;
            for (j = 0U; j < scan_limit && j < ctx->store->entry_count; ++j) {
                const omc_entry* other_entry;
                omc_const_bytes other_key;
                char ingredient_prefix[OMC_JUMBF_PATH_CAP];
                omc_u32 ingredient_prefix_len;
                omc_jumbf_ing_proj ingredient;
                char root[OMC_JUMBF_PATH_CAP];
                omc_u32 root_len;

                other_entry = &ctx->store->entries[j];
                if (other_entry->origin.block != ctx->block
                    || other_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                    continue;
                }
                other_key = omc_jumbf_entry_key_view(ctx->store, other_entry);
                if (omc_jumbf_view_starts_with(other_key, claim_prefix)
                    && (other_key.size == claim_prefix_len
                        || other_key.data[claim_prefix_len] == (omc_u8)'.'
                        || other_key.data[claim_prefix_len] == (omc_u8)'[')
                    && (omc_jumbf_view_has_segment(other_key, "signature")
                        || omc_jumbf_view_has_segment(other_key, "signatures"))) {
                    claim_signature_key_hits += 1U;
                }
                if (!claim_has_claim_generator
                    && other_entry->value.kind == OMC_VAL_TEXT
                    && omc_jumbf_key_matches_field(other_key, claim_prefix,
                                                   claim_prefix_len,
                                                   "claim_generator")) {
                    omc_const_bytes value_view;

                    value_view = omc_jumbf_entry_value_view(ctx->store,
                                                            other_entry);
                    if (value_view.size < sizeof(claim_generator_text)
                        && omc_jumbf_bytes_all_ascii_printable(
                               value_view.data, (omc_u32)value_view.size)) {
                        memcpy(claim_generator_text, value_view.data,
                               value_view.size);
                        claim_generator_text[value_view.size] = '\0';
                        claim_generator_text_len = (omc_u32)value_view.size;
                        claim_has_claim_generator = 1;
                    }
                }
                {
                    char assertion_prefix[OMC_JUMBF_PATH_CAP];
                    omc_u32 assertion_prefix_len;

                    if (omc_jumbf_find_indexed_segment_prefix(
                            other_key, ".assertions[", assertion_prefix,
                            (omc_u32)sizeof(assertion_prefix),
                            &assertion_prefix_len)
                        && omc_jumbf_prefix_is_nested_under(
                               assertion_prefix, assertion_prefix_len,
                               claim_prefix, claim_prefix_len)
                        && !omc_jumbf_prefix_seen_before(
                               ctx->store, ctx->block, scan_limit, j,
                               ".assertions[", assertion_prefix,
                               assertion_prefix_len)) {
                        char assertion_root[OMC_JUMBF_PATH_CAP];
                        omc_u32 assertion_root_len;
                        omc_u64 key_hits;

                        key_hits = omc_jumbf_count_keys_in_prefix(
                            ctx->store, ctx->block, scan_limit,
                            assertion_prefix, assertion_prefix_len);
                        if (!omc_jumbf_build_indexed_root(
                                "c2pa.semantic.claim", claim_sem_index,
                                "assertion", assertion_root,
                                (omc_u32)sizeof(assertion_root),
                                &assertion_root_len)
                            || !omc_jumbf_emit_indexed_field_text(
                                   ctx, assertion_root, claim_assertion_index,
                                   "prefix", assertion_prefix,
                                   assertion_prefix_len)
                            || !omc_jumbf_emit_indexed_field_u64(
                                   ctx, assertion_root, claim_assertion_index,
                                   "key_hits", key_hits)) {
                            return 0;
                        }
                        claim_assertion_count += 1U;
                        claim_assertion_index += 1U;
                    }
                }
                {
                    char sig_prefix[OMC_JUMBF_PATH_CAP];
                    omc_u32 sig_prefix_len;

                    if (omc_jumbf_find_indexed_segment_prefix(
                            other_key, ".signatures[", sig_prefix,
                            (omc_u32)sizeof(sig_prefix), &sig_prefix_len)
                        && omc_jumbf_prefix_is_nested_under(
                               sig_prefix, sig_prefix_len, claim_prefix,
                               claim_prefix_len)
                        && !omc_jumbf_prefix_seen_before(
                               ctx->store, ctx->block, scan_limit, j,
                               ".signatures[", sig_prefix, sig_prefix_len)) {
                        omc_jumbf_sig_proj sig;
                        char sig_root[OMC_JUMBF_PATH_CAP];
                        omc_u32 sig_root_len;

                        if (!omc_jumbf_collect_signature_projection(
                                ctx, scan_limit, sig_prefix, sig_prefix_len,
                                &sig)
                            || !omc_jumbf_build_indexed_root(
                                   "c2pa.semantic.claim", claim_sem_index,
                                   "signature", sig_root,
                                   (omc_u32)sizeof(sig_root), &sig_root_len)
                            || !omc_jumbf_emit_indexed_field_text(
                                   ctx, sig_root, claim_signature_index,
                                   "prefix", sig.prefix, sig.prefix_len)
                            || !omc_jumbf_emit_indexed_field_u64(
                                   ctx, sig_root, claim_signature_index,
                                   "key_hits", sig.key_hits)) {
                            return 0;
                        }
                        if (sig.has_algorithm
                            && !omc_jumbf_emit_indexed_field_text(
                                   ctx, sig_root, claim_signature_index,
                                   "algorithm", sig.algorithm,
                                   sig.algorithm_len)) {
                            return 0;
                        }
                        claim_signature_count += 1U;
                        claim_signature_index += 1U;
                    }
                }
                if (!omc_jumbf_find_indexed_segment_prefix(
                        other_key, ".ingredients[", ingredient_prefix,
                        (omc_u32)sizeof(ingredient_prefix),
                        &ingredient_prefix_len)
                    || !omc_jumbf_prefix_is_nested_under(
                           ingredient_prefix, ingredient_prefix_len,
                           claim_prefix, claim_prefix_len)
                    || omc_jumbf_prefix_seen_before(
                           ctx->store, ctx->block, scan_limit, j,
                           ".ingredients[", ingredient_prefix,
                           ingredient_prefix_len)) {
                    continue;
                }
                if (!omc_jumbf_collect_ingredient_projection(
                        ctx, scan_limit, ingredient_prefix,
                        ingredient_prefix_len, &ingredient)) {
                    return 0;
                }
                if (!omc_jumbf_build_indexed_root(
                        "c2pa.semantic.claim", claim_sem_index, "ingredient",
                        root, (omc_u32)sizeof(root), &root_len)
                    || !omc_jumbf_emit_claim_ingredient_projection(
                           ctx, root, claim_ingredient_index, &ingredient)) {
                    return 0;
                }
                claim_ingredient_index += 1U;
            }

            if (!omc_jumbf_emit_scoped_field_text(ctx, claim_scope, "prefix",
                                                  claim_prefix,
                                                  claim_prefix_len)
                || !omc_jumbf_emit_scoped_field_u64(
                       ctx, claim_scope, "assertion_count",
                       claim_assertion_count)
                || !omc_jumbf_emit_scoped_field_u64(
                       ctx, claim_scope, "signature_count",
                       claim_signature_count)
                || !omc_jumbf_emit_scoped_field_u64(
                       ctx, claim_scope, "signature_key_hits",
                       claim_signature_key_hits)
                || !omc_jumbf_emit_indexed_field_u64(
                    ctx, "c2pa.semantic.claim", claim_sem_index,
                    "ingredient_count", claim_summary.ingredient_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "ingredient_relationship_count",
                       claim_summary.relationship_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "ingredient_thumbnail_url_count",
                       claim_summary.thumbnail_url_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "referenced_by_signature_count",
                       referenced_by_signature_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_signature_count",
                       linked_signature_count_for_claim)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_direct_ingredient_signature_count",
                       linked_direct_signature_count_for_claim)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_cross_ingredient_signature_count",
                       linked_cross_signature_count_for_claim)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_title_count",
                       linked_claim_summary.title_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_relationship_count",
                       linked_claim_summary.relationship_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_thumbnail_url_count",
                       linked_claim_summary.thumbnail_url_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_signature_count",
                       linked_explicit_reference_signature_count_for_claim)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_direct_signature_count",
                       linked_explicit_reference_direct_signature_count_for_claim)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_cross_signature_count",
                       linked_explicit_reference_cross_signature_count_for_claim)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_title_count",
                       claim_link_stats
                           .linked_explicit_reference_summary.title_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_relationship_count",
                       claim_link_stats
                           .linked_explicit_reference_summary.relationship_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_thumbnail_url_count",
                       claim_link_stats
                           .linked_explicit_reference_summary.thumbnail_url_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_unresolved_signature_count",
                       claim_link_stats
                           .linked_explicit_reference_unresolved_signature_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_unresolved_direct_signature_count",
                       claim_link_stats
                           .linked_explicit_reference_unresolved_direct_signature_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_unresolved_cross_signature_count",
                       claim_link_stats
                           .linked_explicit_reference_unresolved_cross_signature_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_unresolved_title_count",
                       claim_link_stats
                           .linked_explicit_reference_unresolved_summary.title_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_unresolved_relationship_count",
                       claim_link_stats
                           .linked_explicit_reference_unresolved_summary
                           .relationship_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_unresolved_thumbnail_url_count",
                       claim_link_stats
                           .linked_explicit_reference_unresolved_summary
                           .thumbnail_url_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_ambiguous_signature_count",
                       claim_link_stats
                           .linked_explicit_reference_ambiguous_signature_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_ambiguous_direct_signature_count",
                       claim_link_stats
                           .linked_explicit_reference_ambiguous_direct_signature_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_ambiguous_cross_signature_count",
                       claim_link_stats
                           .linked_explicit_reference_ambiguous_cross_signature_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_ambiguous_title_count",
                       claim_link_stats
                           .linked_explicit_reference_ambiguous_summary.title_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_ambiguous_relationship_count",
                       claim_link_stats
                           .linked_explicit_reference_ambiguous_summary
                           .relationship_count)
                || !omc_jumbf_emit_indexed_field_u64(
                       ctx, "c2pa.semantic.claim", claim_sem_index,
                       "linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count",
                       claim_link_stats
                           .linked_explicit_reference_ambiguous_summary
                           .thumbnail_url_count)
                || !omc_jumbf_emit_named_counts(
                       ctx, claim_scope, "ingredient_relationship",
                       &claim_summary)
                || !omc_jumbf_emit_named_counts(
                       ctx, claim_scope,
                       "linked_ingredient_relationship",
                       &claim_link_stats.linked_summary)
                || !omc_jumbf_emit_named_counts(
                       ctx, claim_scope,
                       "linked_ingredient_explicit_reference_relationship",
                       &claim_link_stats.linked_explicit_reference_summary)
                || !omc_jumbf_emit_named_counts(
                       ctx, claim_scope,
                       "linked_ingredient_explicit_reference_unresolved_relationship",
                       &claim_link_stats
                            .linked_explicit_reference_unresolved_summary)
                || !omc_jumbf_emit_named_counts(
                       ctx, claim_scope,
                       "linked_ingredient_explicit_reference_ambiguous_relationship",
                       &claim_link_stats
                            .linked_explicit_reference_ambiguous_summary)) {
                return 0;
            }
            if (claim_has_claim_generator
                && !omc_jumbf_emit_scoped_field_text(
                       ctx, claim_scope, "claim_generator",
                       claim_generator_text, claim_generator_text_len)) {
                return 0;
            }
            claim_sem_index += 1U;
        }
    }
    {
        omc_u32 manifest_sem_index;

        manifest_sem_index = 0U;
        for (i = 0U; i < scan_limit && i < ctx->store->entry_count; ++i) {
            const omc_entry* entry;
            omc_const_bytes key;
            char manifest_prefix[OMC_JUMBF_PATH_CAP];
            omc_u32 manifest_prefix_len;
            int manifest_is_active;
            omc_u64 manifest_claim_count;
            omc_u64 manifest_assertion_count;
            omc_u64 manifest_signature_count;
            omc_u64 manifest_signature_linked_count;
            omc_u64 manifest_cross_claim_link_count;
            omc_u64 manifest_ingredient_claim_count;
            omc_u64 manifest_ingredient_claim_with_signature_count;
            omc_u64 manifest_ingredient_claim_referenced_by_signature_count;
            omc_u64 manifest_ingredient_signature_count;
            omc_u64 manifest_ingredient_linked_signature_count;
            omc_u64 manifest_ingredient_linked_claim_count;
            omc_u64 manifest_ingredient_linked_direct_claim_count;
            omc_u64 manifest_ingredient_linked_cross_claim_count;
            omc_u64 manifest_ingredient_linked_claim_mixed_source_count;
            omc_u64 manifest_ingredient_linked_signature_direct_source_count;
            omc_u64 manifest_ingredient_linked_signature_cross_source_count;
            omc_u64 manifest_ingredient_linked_signature_mixed_source_count;
            omc_u64 manifest_ingredient_explicit_reference_signature_count;
            omc_u64
                manifest_ingredient_explicit_reference_unresolved_signature_count;
            omc_u64
                manifest_ingredient_explicit_reference_ambiguous_signature_count;
            omc_u64 manifest_explicit_reference_signature_count;
            omc_u64 manifest_explicit_reference_unresolved_signature_count;
            omc_u64 manifest_explicit_reference_ambiguous_signature_count;
            omc_u64 manifest_explicit_reference_index_hits;
            omc_u64 manifest_explicit_reference_label_hits;
            omc_jumbf_ing_summary manifest_summary;
            omc_jumbf_ing_summary manifest_linked_summary;
            omc_jumbf_ing_summary manifest_linked_direct_summary;
            omc_jumbf_ing_summary manifest_linked_cross_summary;
            omc_jumbf_exp_ref_scope_stats manifest_exp_ref_stats;
            omc_size j;
            char manifest_scope[OMC_JUMBF_PATH_CAP];
            omc_u32 manifest_scope_len;

            entry = &ctx->store->entries[i];
            if (entry->origin.block != ctx->block
                || entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                continue;
            }
            key = omc_jumbf_entry_key_view(ctx->store, entry);
            if (!omc_jumbf_extract_manifest_prefix_from_key(
                    key, manifest_prefix, (omc_u32)sizeof(manifest_prefix),
                    &manifest_prefix_len)
                || omc_jumbf_manifest_prefix_seen_before(
                       ctx->store, ctx->block, scan_limit, i, manifest_prefix,
                       manifest_prefix_len)) {
                continue;
            }

            manifest_is_active = omc_jumbf_manifest_prefix_is_active(
                manifest_prefix, manifest_prefix_len);
            manifest_claim_count = 0U;
            manifest_assertion_count = 0U;
            manifest_signature_count = 0U;
            manifest_signature_linked_count = 0U;
            manifest_cross_claim_link_count = 0U;
            manifest_ingredient_claim_count = 0U;
            manifest_ingredient_claim_with_signature_count = 0U;
            manifest_ingredient_claim_referenced_by_signature_count = 0U;
            manifest_ingredient_signature_count = 0U;
            manifest_ingredient_linked_signature_count = 0U;
            manifest_ingredient_linked_claim_count = 0U;
            manifest_ingredient_linked_direct_claim_count = 0U;
            manifest_ingredient_linked_cross_claim_count = 0U;
            manifest_ingredient_linked_claim_mixed_source_count = 0U;
            manifest_ingredient_linked_signature_direct_source_count = 0U;
            manifest_ingredient_linked_signature_cross_source_count = 0U;
            manifest_ingredient_linked_signature_mixed_source_count = 0U;
            manifest_ingredient_explicit_reference_signature_count = 0U;
            manifest_ingredient_explicit_reference_unresolved_signature_count
                = 0U;
            manifest_ingredient_explicit_reference_ambiguous_signature_count
                = 0U;
            manifest_explicit_reference_signature_count = 0U;
            manifest_explicit_reference_unresolved_signature_count = 0U;
            manifest_explicit_reference_ambiguous_signature_count = 0U;
            manifest_explicit_reference_index_hits = 0U;
            manifest_explicit_reference_label_hits = 0U;
            omc_jumbf_ing_summary_init(&manifest_summary);
            omc_jumbf_ing_summary_init(&manifest_linked_summary);
            omc_jumbf_ing_summary_init(&manifest_linked_direct_summary);
            omc_jumbf_ing_summary_init(&manifest_linked_cross_summary);
            omc_jumbf_exp_ref_scope_stats_init(&manifest_exp_ref_stats);
            if (!omc_jumbf_build_indexed_scope(
                    "c2pa.semantic.manifest", manifest_sem_index,
                    manifest_scope, (omc_u32)sizeof(manifest_scope),
                    &manifest_scope_len)) {
                return 0;
            }
            for (j = 0U; j < scan_limit && j < ctx->store->entry_count; ++j) {
                const omc_entry* claim_entry;
                omc_const_bytes claim_key;
                char claim_prefix[OMC_JUMBF_PATH_CAP];
                omc_u32 claim_prefix_len;
                char claim_manifest_prefix[OMC_JUMBF_PATH_CAP];
                omc_u32 claim_manifest_prefix_len;

                claim_entry = &ctx->store->entries[j];
                if (claim_entry->origin.block != ctx->block
                    || claim_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                    continue;
                }
                claim_key = omc_jumbf_entry_key_view(ctx->store, claim_entry);
                if (!omc_jumbf_find_indexed_segment_prefix(
                        claim_key, ".claims[", claim_prefix,
                        (omc_u32)sizeof(claim_prefix), &claim_prefix_len)
                    || omc_jumbf_prefix_seen_before(
                           ctx->store, ctx->block, scan_limit, j,
                           ".claims[", claim_prefix, claim_prefix_len)
                    || !omc_jumbf_extract_manifest_prefix_from_claim_prefix(
                           claim_prefix, claim_prefix_len,
                           claim_manifest_prefix,
                           (omc_u32)sizeof(claim_manifest_prefix),
                           &claim_manifest_prefix_len)
                    || claim_manifest_prefix_len != manifest_prefix_len
                    || memcmp(claim_manifest_prefix, manifest_prefix,
                              manifest_prefix_len)
                           != 0) {
                    continue;
                }
                manifest_claim_count += 1U;
                manifest_assertion_count += omc_jumbf_count_nested_indexed_prefixes(
                    ctx->store, ctx->block, scan_limit, claim_prefix,
                    claim_prefix_len, ".assertions[");
                {
                    omc_jumbf_ing_summary claim_summary;
                    omc_jumbf_claim_signature_stats claim_sig_stats;
                    omc_u64 nested_signature_count;
                    omc_u64 referenced_by_signature_count;
                    omc_u64 linked_signature_count_for_claim;
                    omc_u64 linked_direct_signature_count_for_claim;
                    omc_u64 linked_cross_signature_count_for_claim;
                    omc_u64 linked_explicit_reference_direct_signature_count_for_claim;
                    omc_u64 linked_explicit_reference_cross_signature_count_for_claim;
                    omc_jumbf_ing_summary linked_claim_summary;
                    omc_jumbf_ing_summary linked_claim_direct_summary;
                    omc_jumbf_ing_summary linked_claim_cross_summary;
                    omc_jumbf_claim_link_stats claim_link_stats;

                    if (!omc_jumbf_collect_claim_ingredient_summary(
                            ctx, scan_limit, claim_prefix, claim_prefix_len,
                            &claim_summary)
                        || !omc_jumbf_collect_claim_signature_stats(
                               ctx, scan_limit, claim_prefix, claim_prefix_len,
                               &claim_sig_stats)
                        || !omc_jumbf_collect_claim_signature_topology(
                               ctx, scan_limit, claim_prefix, claim_prefix_len,
                               &claim_link_stats)) {
                        return 0;
                    }
                    manifest_explicit_reference_signature_count
                        += claim_sig_stats.explicit_reference_signature_count;
                    manifest_explicit_reference_unresolved_signature_count
                        += claim_sig_stats
                               .explicit_reference_unresolved_signature_count;
                    manifest_explicit_reference_ambiguous_signature_count
                        += claim_sig_stats
                               .explicit_reference_ambiguous_signature_count;
                    manifest_explicit_reference_index_hits
                        += claim_sig_stats.explicit_reference_index_hits;
                    manifest_explicit_reference_label_hits
                        += claim_sig_stats.explicit_reference_label_hits;
                    nested_signature_count
                        = claim_link_stats.nested_signature_count;
                    referenced_by_signature_count
                        = claim_link_stats.referenced_by_signature_count;
                    linked_signature_count_for_claim
                        = claim_link_stats.linked_signature_count;
                    linked_direct_signature_count_for_claim
                        = claim_link_stats.linked_direct_signature_count;
                    linked_cross_signature_count_for_claim
                        = claim_link_stats.linked_cross_signature_count;
                    linked_explicit_reference_direct_signature_count_for_claim
                        = claim_link_stats
                              .linked_explicit_reference_direct_signature_count;
                    linked_explicit_reference_cross_signature_count_for_claim
                        = claim_link_stats
                              .linked_explicit_reference_cross_signature_count;
                    linked_claim_summary = claim_link_stats.linked_summary;
                    linked_claim_direct_summary
                        = claim_link_stats.linked_direct_summary;
                    linked_claim_cross_summary
                        = claim_link_stats.linked_cross_summary;
                    (void)linked_explicit_reference_direct_signature_count_for_claim;
                    (void)linked_explicit_reference_cross_signature_count_for_claim;
                    omc_jumbf_ing_summary_merge(&manifest_summary,
                                                &claim_summary);
                    if (claim_summary.ingredient_count != 0U) {
                        manifest_ingredient_claim_count += 1U;
                        manifest_ingredient_signature_count
                            += nested_signature_count;
                        manifest_ingredient_explicit_reference_unresolved_signature_count
                            += claim_sig_stats
                                   .explicit_reference_unresolved_signature_count;
                        manifest_ingredient_explicit_reference_ambiguous_signature_count
                            += claim_sig_stats
                                   .explicit_reference_ambiguous_signature_count;
                        if (nested_signature_count != 0U) {
                            manifest_ingredient_claim_with_signature_count
                                += 1U;
                        }
                        if (referenced_by_signature_count != 0U) {
                            manifest_ingredient_claim_referenced_by_signature_count
                                += 1U;
                        }
                        if (linked_signature_count_for_claim != 0U) {
                            manifest_ingredient_linked_claim_count += 1U;
                            manifest_ingredient_linked_signature_count
                                += linked_signature_count_for_claim;
                            omc_jumbf_ing_summary_merge(
                                &manifest_linked_summary,
                                &linked_claim_summary);
                            manifest_ingredient_linked_signature_direct_source_count
                                += linked_direct_signature_count_for_claim;
                            manifest_ingredient_linked_signature_cross_source_count
                                += linked_cross_signature_count_for_claim;
                            if (linked_direct_signature_count_for_claim != 0U
                                && linked_cross_signature_count_for_claim
                                       != 0U) {
                                manifest_ingredient_linked_signature_mixed_source_count
                                    += 1U;
                            }
                            manifest_ingredient_explicit_reference_signature_count
                                += claim_link_stats
                                       .linked_explicit_reference_signature_count;
                            omc_jumbf_exp_ref_scope_stats_note_claim(
                                &manifest_exp_ref_stats, &claim_link_stats);
                            if (linked_direct_signature_count_for_claim != 0U) {
                                manifest_ingredient_linked_direct_claim_count
                                    += 1U;
                                omc_jumbf_ing_summary_merge(
                                    &manifest_linked_direct_summary,
                                    &linked_claim_direct_summary);
                            }
                            if (linked_cross_signature_count_for_claim != 0U) {
                                manifest_ingredient_linked_cross_claim_count
                                    += 1U;
                                omc_jumbf_ing_summary_merge(
                                    &manifest_linked_cross_summary,
                                    &linked_claim_cross_summary);
                            }
                            if (linked_direct_signature_count_for_claim != 0U
                                && linked_cross_signature_count_for_claim
                                       != 0U) {
                                manifest_ingredient_linked_claim_mixed_source_count
                                    += 1U;
                            }
                        }
                    }
                }
            }

            for (j = 0U; j < scan_limit && j < ctx->store->entry_count; ++j) {
                const omc_entry* sig_entry;
                omc_const_bytes sig_key;
                char sig_prefix[OMC_JUMBF_PATH_CAP];
                omc_u32 sig_prefix_len;
                omc_jumbf_sig_proj sig;
                omc_jumbf_sig_link_proj sig_link;

                sig_entry = &ctx->store->entries[j];
                if (sig_entry->origin.block != ctx->block
                    || sig_entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
                    continue;
                }
                sig_key = omc_jumbf_entry_key_view(ctx->store, sig_entry);
                if (!omc_jumbf_find_indexed_segment_prefix(
                        sig_key, ".signatures[", sig_prefix,
                        (omc_u32)sizeof(sig_prefix), &sig_prefix_len)
                    || !omc_jumbf_prefix_is_nested_under(
                           sig_prefix, sig_prefix_len, manifest_prefix,
                           manifest_prefix_len)
                    || omc_jumbf_prefix_seen_before(
                           ctx->store, ctx->block, scan_limit, j,
                           ".signatures[", sig_prefix, sig_prefix_len)
                    || !omc_jumbf_collect_signature_projection(
                           ctx, scan_limit, sig_prefix, sig_prefix_len, &sig)
                    || !omc_jumbf_collect_signature_ingredient_link(
                           ctx, scan_limit, &sig, &sig_link)) {
                    if (ctx->res.status != OMC_JUMBF_OK) {
                        return 0;
                    }
                    continue;
                }
                manifest_signature_count += 1U;
                if (sig_link.target_claim_count != 0U) {
                    manifest_signature_linked_count += 1U;
                }
                manifest_cross_claim_link_count
                    += sig_link.target_cross_claim_count;
            }

            if (manifest_claim_count != 0U
                || manifest_summary.ingredient_count != 0U) {
                if (!omc_jumbf_emit_scoped_field_text(
                        ctx, manifest_scope, "prefix", manifest_prefix,
                        manifest_prefix_len)
                    || !omc_jumbf_emit_scoped_field_u8(
                           ctx, manifest_scope, "is_active",
                           (omc_u8)(manifest_is_active ? 1U : 0U))
                    || !omc_jumbf_emit_scoped_field_u64(
                           ctx, manifest_scope, "assertion_count",
                           manifest_assertion_count)
                    || !omc_jumbf_emit_scoped_field_u64(
                           ctx, manifest_scope, "signature_count",
                           manifest_signature_count)
                    || !omc_jumbf_emit_scoped_field_u64(
                           ctx, manifest_scope, "signature_linked_count",
                           manifest_signature_linked_count)
                    || !omc_jumbf_emit_scoped_field_u64(
                           ctx, manifest_scope, "signature_orphan_count",
                           manifest_signature_count
                                   >= manifest_signature_linked_count
                               ? (manifest_signature_count
                                  - manifest_signature_linked_count)
                               : 0U)
                    || !omc_jumbf_emit_scoped_field_u64(
                           ctx, manifest_scope, "cross_claim_link_count",
                           manifest_cross_claim_link_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                        ctx, "c2pa.semantic.manifest", manifest_sem_index,
                        "claim_count", manifest_claim_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "explicit_reference_signature_count",
                           manifest_explicit_reference_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "explicit_reference_unresolved_signature_count",
                           manifest_explicit_reference_unresolved_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "explicit_reference_ambiguous_signature_count",
                           manifest_explicit_reference_ambiguous_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "explicit_reference_index_hits",
                           manifest_explicit_reference_index_hits)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "explicit_reference_label_hits",
                           manifest_explicit_reference_label_hits)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_count", manifest_summary.ingredient_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_relationship_count",
                           manifest_summary.relationship_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_thumbnail_url_count",
                           manifest_summary.thumbnail_url_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_claim_count",
                           manifest_ingredient_claim_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_claim_with_signature_count",
                           manifest_ingredient_claim_with_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_claim_referenced_by_signature_count",
                           manifest_ingredient_claim_referenced_by_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_signature_count",
                           manifest_ingredient_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_count",
                           manifest_ingredient_linked_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_claim_count",
                           manifest_ingredient_linked_claim_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_direct_claim_count",
                           manifest_ingredient_linked_direct_claim_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_claim_direct_source_count",
                           manifest_ingredient_linked_direct_claim_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_cross_claim_count",
                           manifest_ingredient_linked_cross_claim_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_claim_cross_source_count",
                           manifest_ingredient_linked_cross_claim_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_claim_mixed_source_count",
                           manifest_ingredient_linked_claim_mixed_source_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_direct_source_count",
                           manifest_ingredient_linked_signature_direct_source_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_cross_source_count",
                           manifest_ingredient_linked_signature_cross_source_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_mixed_source_count",
                           manifest_ingredient_linked_signature_mixed_source_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_title_count",
                           manifest_linked_summary.title_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_relationship_count",
                           manifest_linked_summary.relationship_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_thumbnail_url_count",
                           manifest_linked_summary.thumbnail_url_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_direct_title_count",
                           manifest_linked_direct_summary.title_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_direct_relationship_count",
                           manifest_linked_direct_summary.relationship_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_direct_thumbnail_url_count",
                           manifest_linked_direct_summary.thumbnail_url_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_cross_title_count",
                           manifest_linked_cross_summary.title_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_cross_relationship_count",
                           manifest_linked_cross_summary.relationship_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_linked_signature_cross_thumbnail_url_count",
                           manifest_linked_cross_summary.thumbnail_url_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_explicit_reference_signature_count",
                           manifest_ingredient_explicit_reference_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_explicit_reference_unresolved_signature_count",
                           manifest_ingredient_explicit_reference_unresolved_signature_count)
                    || !omc_jumbf_emit_indexed_field_u64(
                           ctx, "c2pa.semantic.manifest", manifest_sem_index,
                           "ingredient_explicit_reference_ambiguous_signature_count",
                           manifest_ingredient_explicit_reference_ambiguous_signature_count)
                    || !omc_jumbf_emit_named_counts(
                           ctx, manifest_scope, "ingredient_relationship",
                           &manifest_summary)
                    || !omc_jumbf_emit_named_counts(
                           ctx, manifest_scope,
                           "ingredient_linked_signature_relationship",
                           &manifest_linked_summary)
                    || !omc_jumbf_emit_exp_ref_scope_fields(
                           ctx, manifest_scope, &manifest_exp_ref_stats)) {
                    return 0;
                }
                manifest_sem_index += 1U;
            }
        }
    }
    if (have_claim_generator
        && !omc_jumbf_emit_field_text(
               ctx, "c2pa.semantic.claim_generator",
               omc_jumbf_cstr_size("c2pa.semantic.claim_generator"),
               claim_generator, claim_generator_len)) {
        return 0;
    }
    {
        omc_c2pa_profile_summary verify_summary;

        omc_c2pa_profile_summary_init(&verify_summary);
        verify_summary.available = 1;
        verify_summary.manifest_present = has_manifest ? 1U : 0U;
        verify_summary.claim_present = has_claim ? 1U : 0U;
        verify_summary.signature_present = has_signature ? 1U : 0U;
        verify_summary.claim_count = claim_count;
        verify_summary.signature_count = signature_count;
        verify_summary.signature_linked = signature_linked_count;
        verify_summary.signature_orphan
            = signature_count >= signature_linked_count
                  ? (signature_count - signature_linked_count)
                  : 0U;
        verify_summary.explicit_reference_signature_count
            = explicit_reference_signature_count;
        verify_summary.explicit_reference_unresolved_signature_count
            = explicit_reference_unresolved_signature_count;
        verify_summary.explicit_reference_ambiguous_signature_count
            = explicit_reference_ambiguous_signature_count;
        return omc_jumbf_emit_c2pa_verify_scaffold(
            ctx, signature_count != 0U || has_signature, ctx->c2pa_emitted,
            &verify_summary);
    }
}

static int omc_jumbf_decode_boxes(omc_jumbf_ctx* ctx, const omc_u8* bytes,
                                  omc_size size, omc_u64 begin, omc_u64 end,
                                  omc_u32 depth, const char* parent_path,
                                  omc_u32 parent_len);

static int
omc_jumbf_emit_box_fields(omc_jumbf_ctx* ctx, const omc_jumbf_box* box,
                          const char* box_path, omc_u32 box_path_len)
{
    char field_key[OMC_JUMBF_PATH_CAP];
    char type_text[5];
    omc_u32 field_len;
    omc_u64 payload_size;

    payload_size = box->size - box->header_size;
    omc_jumbf_fourcc_text(box->type, type_text);

    if (!omc_jumbf_make_field_key(box_path, box_path_len, "type", field_key,
                                  (omc_u32)sizeof(field_key), &field_len)
        || !omc_jumbf_emit_field_text(ctx, field_key, field_len, type_text,
                                      4U)) {
        return 0;
    }
    if (!omc_jumbf_make_field_key(box_path, box_path_len, "size", field_key,
                                  (omc_u32)sizeof(field_key), &field_len)
        || !omc_jumbf_emit_field_u64(ctx, field_key, field_len, box->size)) {
        return 0;
    }
    if (!omc_jumbf_make_field_key(box_path, box_path_len, "payload_size",
                                  field_key, (omc_u32)sizeof(field_key),
                                  &field_len)
        || !omc_jumbf_emit_field_u64(ctx, field_key, field_len, payload_size)) {
        return 0;
    }
    if (!omc_jumbf_make_field_key(box_path, box_path_len, "offset", field_key,
                                  (omc_u32)sizeof(field_key), &field_len)
        || !omc_jumbf_emit_field_u64(ctx, field_key, field_len, box->offset)) {
        return 0;
    }
    return 1;
}

static int
omc_jumbf_decode_boxes(omc_jumbf_ctx* ctx, const omc_u8* bytes, omc_size size,
                       omc_u64 begin, omc_u64 end, omc_u32 depth,
                       const char* parent_path, omc_u32 parent_len)
{
    omc_u64 off;
    omc_u32 child_index;

    if (ctx == (omc_jumbf_ctx*)0 || bytes == (const omc_u8*)0) {
        return 0;
    }
    if (depth > ctx->opts.limits.max_box_depth) {
        ctx->res.status = OMC_JUMBF_LIMIT;
        return 0;
    }

    off = begin;
    child_index = 0U;
    while (off < end) {
        omc_jumbf_box box;
        omc_u64 payload_off;
        omc_u64 payload_size;
        char box_path[OMC_JUMBF_PATH_CAP];
        omc_u32 box_path_len;

        if (!omc_jumbf_parse_box(bytes, size, off, end, &box)
            || !omc_jumbf_take_box(ctx)
            || !omc_jumbf_make_child_path(parent_path, parent_len, child_index,
                                          box_path,
                                          (omc_u32)sizeof(box_path),
                                          &box_path_len)
            || !omc_jumbf_emit_box_fields(ctx, &box, box_path, box_path_len)) {
            if (ctx->res.status == OMC_JUMBF_OK) {
                ctx->res.status = OMC_JUMBF_MALFORMED;
            }
            return 0;
        }

        payload_off = box.offset + box.header_size;
        payload_size = box.size - box.header_size;

        if (box.type == OMC_FOURCC('j', 'u', 'm', 'd')
            && parent_path != (const char*)0 && parent_len != 0U) {
            char label[257];
            omc_u32 label_len;
            char label_key[OMC_JUMBF_PATH_CAP];
            omc_u32 label_key_len;

            if (omc_jumbf_parse_jumd_label(bytes + (omc_size)payload_off,
                                           payload_size, label,
                                           (omc_u32)sizeof(label),
                                           &label_len)) {
                if (!omc_jumbf_make_field_key(parent_path, parent_len, "label",
                                              label_key,
                                              (omc_u32)sizeof(label_key),
                                              &label_key_len)
                    || !omc_jumbf_emit_field_text(ctx, label_key, label_key_len,
                                                  label, label_len)) {
                    if (ctx->res.status == OMC_JUMBF_OK) {
                        ctx->res.status = OMC_JUMBF_LIMIT;
                    }
                    return 0;
                }
            }
        }

        if (ctx->opts.detect_c2pa) {
            if (box.type == OMC_FOURCC('c', '2', 'p', 'a')) {
                if (!omc_jumbf_emit_c2pa_marker(ctx, box_path, box_path_len)) {
                    return 0;
                }
            } else if (box.type == OMC_FOURCC('j', 'u', 'm', 'd')) {
                if (omc_jumbf_ascii_icase_contains(
                        bytes + (omc_size)payload_off, payload_size, "c2pa",
                        4096U)) {
                    if (!omc_jumbf_emit_c2pa_marker(ctx, box_path,
                                                    box_path_len)) {
                        return 0;
                    }
                }
            }
        }

        if (ctx->opts.decode_cbor
            && box.type == OMC_FOURCC('c', 'b', 'o', 'r')) {
            char cbor_path[OMC_JUMBF_PATH_CAP];
            omc_u32 cbor_path_len;

            if (!omc_jumbf_make_field_key(box_path, box_path_len, "cbor",
                                          cbor_path,
                                          (omc_u32)sizeof(cbor_path),
                                          &cbor_path_len)) {
                ctx->res.status = OMC_JUMBF_LIMIT;
                return 0;
            }
            if (!omc_jumbf_decode_cbor_payload(
                    ctx, bytes + (omc_size)payload_off, (omc_size)payload_size,
                    cbor_path, cbor_path_len)) {
                return 0;
            }
        }

        if (payload_size >= 8U
            && omc_jumbf_looks_like_bmff_sequence(bytes, size, payload_off,
                                                  payload_off + payload_size)) {
            if (!omc_jumbf_decode_boxes(ctx, bytes, size, payload_off,
                                        payload_off + payload_size, depth + 1U,
                                        box_path, box_path_len)) {
                return 0;
            }
        }

        off += box.size;
        child_index += 1U;
        if (box.size == 0U) {
            break;
        }
    }

    return 1;
}

void
omc_jumbf_opts_init(omc_jumbf_opts* opts)
{
    if (opts == (omc_jumbf_opts*)0) {
        return;
    }

    memset(opts, 0, sizeof(*opts));
    opts->decode_cbor = 1;
    opts->detect_c2pa = 1;
    opts->verify_backend = OMC_C2PA_VERIFY_BACKEND_AUTO;
    opts->limits.max_input_bytes = OMC_JUMBF_DEF_MAX_INPUT_BYTES;
    opts->limits.max_box_depth = OMC_JUMBF_DEF_MAX_BOX_DEPTH;
    opts->limits.max_boxes = OMC_JUMBF_DEF_MAX_BOXES;
    opts->limits.max_entries = OMC_JUMBF_DEF_MAX_ENTRIES;
    opts->limits.max_cbor_depth = OMC_JUMBF_DEF_MAX_CBOR_DEPTH;
    opts->limits.max_cbor_items = OMC_JUMBF_DEF_MAX_CBOR_ITEMS;
    opts->limits.max_cbor_key_bytes = OMC_JUMBF_DEF_MAX_CBOR_KEY_BYTES;
    opts->limits.max_cbor_text_bytes = OMC_JUMBF_DEF_MAX_CBOR_TEXT_BYTES;
    opts->limits.max_cbor_bytes_bytes = OMC_JUMBF_DEF_MAX_CBOR_BYTES_BYTES;
}

static omc_jumbf_res
omc_jumbf_run(const omc_u8* bytes, omc_size size, omc_store* store,
              omc_block_id source_block, omc_entry_flags flags,
              const omc_jumbf_opts* opts, int measure_only)
{
    omc_jumbf_ctx ctx;
    omc_size scan_limit;

    memset(&ctx, 0, sizeof(ctx));
    ctx.res.status = OMC_JUMBF_UNSUPPORTED;
    ctx.store = measure_only ? (omc_store*)0 : store;
    ctx.block = source_block;
    ctx.flags = flags;
    ctx.input = bytes;
    ctx.input_size = size;
    omc_jumbf_opts_norm(&ctx.opts, opts);

    if (bytes == (const omc_u8*)0 || (!measure_only && store == (omc_store*)0)) {
        ctx.res.status = OMC_JUMBF_MALFORMED;
        return ctx.res;
    }
    if (ctx.opts.limits.max_input_bytes != 0U
        && (omc_u64)size > ctx.opts.limits.max_input_bytes) {
        ctx.res.status = OMC_JUMBF_LIMIT;
        return ctx.res;
    }

    if (!omc_jumbf_looks_like_bmff_sequence(bytes, size, 0U,
                                            (omc_u64)size)) {
        ctx.res.status = OMC_JUMBF_UNSUPPORTED;
        return ctx.res;
    }

    ctx.res.status = OMC_JUMBF_OK;
    if (!omc_jumbf_decode_boxes(&ctx, bytes, size, 0U, (omc_u64)size, 0U,
                                (const char*)0, 0U)) {
        if (ctx.res.status == OMC_JUMBF_OK) {
            ctx.res.status = OMC_JUMBF_MALFORMED;
        }
    } else if (ctx.store != (omc_store*)0) {
        scan_limit = ctx.store->entry_count;
        if (!omc_jumbf_project_c2pa_semantics(&ctx, scan_limit)
            && ctx.res.status == OMC_JUMBF_OK) {
            ctx.res.status = OMC_JUMBF_MALFORMED;
        }
    } else if (!omc_jumbf_project_c2pa_semantics_meas(&ctx, bytes, size)
               && ctx.res.status == OMC_JUMBF_OK) {
        ctx.res.status = OMC_JUMBF_MALFORMED;
    }
    return ctx.res;
}

omc_jumbf_res
omc_jumbf_dec(const omc_u8* bytes, omc_size size, omc_store* store,
              omc_block_id source_block, omc_entry_flags flags,
              const omc_jumbf_opts* opts)
{
    return omc_jumbf_run(bytes, size, store, source_block, flags, opts, 0);
}

omc_jumbf_res
omc_jumbf_meas(const omc_u8* bytes, omc_size size,
               const omc_jumbf_opts* opts)
{
    return omc_jumbf_run(bytes, size, (omc_store*)0, 0U,
                         OMC_ENTRY_FLAG_NONE, opts, 1);
}
