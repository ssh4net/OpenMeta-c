#ifndef OMC_LOCATION_FIXTURE_H
#define OMC_LOCATION_FIXTURE_H
#include "omc/omc_translation.h"
#define OMC_LOCATION_CASE_COUNT 70U
OMC_EXTERN_C_BEGIN
omc_translation_status
omc_location_fixture(omc_u32 index, omc_store *source,
                     omc_location_translation_opts *opts);
void
omc_location_check_success(omc_u32 index, const omc_store *out,
                           const omc_translation_res *res);
OMC_EXTERN_C_END
#endif
