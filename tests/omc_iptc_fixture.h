#ifndef OMC_IPTC_FIXTURE_H
#define OMC_IPTC_FIXTURE_H
#include "omc/omc_translation.h"
#define OMC_IPTC_CASE_COUNT 120U
OMC_EXTERN_C_BEGIN
typedef struct omc_iptc_test_field {
    const char *ns;
    const char *path;
    const char *value;
    omc_u16 dataset;
    omc_u16 max_bytes;
} omc_iptc_test_field;
extern const omc_iptc_test_field omc_iptc_test_fields[20];
omc_translation_status
omc_iptc_fixture(omc_u32 index, omc_store *source, omc_iptc_translation_opts *opts);
void
omc_iptc_check_success(omc_u32 index, const omc_store *out,
                       const omc_translation_res *res);
OMC_EXTERN_C_END
#endif
