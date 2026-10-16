#ifndef OMC_IPTC_SERIALIZE_INTERNAL_H
#define OMC_IPTC_SERIALIZE_INTERNAL_H

#include "omc/omc_iptc_serialize.h"

/* Internal transfer entry point. When skip_unsupported is nonzero, malformed
 * or unsupported active IPTC entries increment entries_skipped and are omitted.
 * Limits remain fatal. Empty output measures; the C store has no finalized
 * state to check. */
omc_iptc_iim_serialize_res
omc_iptc_iim_serialize_internal(
    const omc_store *store, const omc_iptc_iim_serialize_opts *opts,
    int skip_unsupported, omc_mut_bytes output);

#endif
