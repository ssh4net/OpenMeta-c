#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_bmff.h"
#include "omc/omc_capabilities.h"
#include "omc/omc_ccm_query.h"
#include "omc/omc_edit.h"
#include "omc/omc_exif.h"
#include "omc/omc_exif_name.h"
#include "omc/omc_exr.h"
#include "omc/omc_icc.h"
#include "omc/omc_iptc.h"
#include "omc/omc_irb.h"
#include "omc/omc_jumbf.h"
#include "omc/omc_jxl_encoder_handoff.h"
#include "omc/omc_key.h"
#include "omc/omc_pay.h"
#include "omc/omc_preview.h"
#include "omc/omc_read.h"
#include "omc/omc_scan.h"
#include "omc/omc_span.h"
#include "omc/omc_status.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer.h"
#include "omc/omc_transfer_artifact.h"
#include "omc/omc_transfer_package.h"
#include "omc/omc_transfer_payload.h"
#include "omc/omc_transfer_persist.h"
#include "omc/omc_types.h"
#include "omc/omc_val.h"
#include "omc/omc_validate.h"
#include "omc/omc_xmp.h"
#include "omc/omc_xmp_apply.h"
#include "omc/omc_xmp_dump.h"
#include "omc/omc_xmp_embed.h"
#include "omc/omc_xmp_write.h"

#include "omc_test_assert.h"

int
main(void)
{
    OMC_TEST_CHECK_U64_EQ(OMC_METADATA_CAPABILITIES_CONTRACT_VERSION, 1U);
    OMC_TEST_CHECK_U64_EQ(OMC_TRANSFER_TARGET_IMAGE_SPEC_MAX_SAMPLES, 8U);
    return omc_test_finish();
}
