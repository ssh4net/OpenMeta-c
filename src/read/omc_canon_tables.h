/* SPDX-License-Identifier: Apache-2.0
 * Flat Canon decoder tag selectors from OpenMeta C++ f11de3e0. */
#ifndef OMC_CANON_TABLES_H
#define OMC_CANON_TABLES_H
#include "omc/omc_base.h"
#include <string.h>
static int omc_exif_canon_flat_tag(const char *table, omc_u16 tag)
{
    if (strcmp(table, "panorama") == 0)
        return tag == 0x0002U || tag == 0x0005U;
    if (strcmp(table, "cropinfo") == 0)
        return tag == 0x0000U || tag == 0x0001U || tag == 0x0002U || tag == 0x0003U;
    if (strcmp(table, "mycolors") == 0)
        return tag == 0x0002U;
    if (strcmp(table, "sensorinfo") == 0)
        return tag == 0x0001U || tag == 0x0002U || tag == 0x0005U || tag == 0x0006U ||
               tag == 0x0007U || tag == 0x0008U || tag == 0x0009U || tag == 0x000AU ||
               tag == 0x000BU || tag == 0x000CU;
    if (strcmp(table, "processing") == 0)
        return tag == 0x0001U || tag == 0x0002U || tag == 0x0003U || tag == 0x0004U ||
               tag == 0x0005U || tag == 0x0006U || tag == 0x0007U || tag == 0x0008U ||
               tag == 0x0009U || tag == 0x000AU || tag == 0x000BU || tag == 0x000CU ||
               tag == 0x000DU || tag == 0x000EU || tag == 0x000FU;
    if (strcmp(table, "aspectinfo") == 0)
        return tag == 0x0000U || tag == 0x0001U || tag == 0x0002U || tag == 0x0003U ||
               tag == 0x0004U;
    if (strcmp(table, "vignettingcorr2") == 0)
        return tag == 0x0005U || tag == 0x0006U || tag == 0x0007U || tag == 0x0009U;
    if (strcmp(table, "hdrinfo") == 0)
        return tag == 0x0001U || tag == 0x0002U;
    if (strcmp(table, "afconfig") == 0)
        return tag == 0x0001U || tag == 0x0002U || tag == 0x0003U || tag == 0x0004U ||
               tag == 0x0005U || tag == 0x0006U || tag == 0x0007U || tag == 0x0008U ||
               tag == 0x0009U || tag == 0x000AU || tag == 0x000BU || tag == 0x000CU ||
               tag == 0x000DU || tag == 0x000EU || tag == 0x000FU || tag == 0x0010U ||
               tag == 0x0011U || tag == 0x0012U || tag == 0x0013U || tag == 0x0014U ||
               tag == 0x0015U || tag == 0x0018U || tag == 0x001AU || tag == 0x001BU ||
               tag == 0x001CU || tag == 0x001DU || tag == 0x001EU;
    if (strcmp(table, "lightingopt") == 0)
        return tag == 0x0001U || tag == 0x0002U || tag == 0x0003U || tag == 0x0004U ||
               tag == 0x0005U || tag == 0x000AU || tag == 0x000BU;
    if (strcmp(table, "multiexp") == 0)
        return tag == 0x0001U || tag == 0x0002U || tag == 0x0003U;
    if (strcmp(table, "ambience") == 0)
        return tag == 0x0001U;
    if (strcmp(table, "rawburstinfo") == 0)
        return tag == 0x0001U || tag == 0x0002U;
    /* The reference name registry falls back to CanonMain when the
     * unknown focal-length subtable has no registered table. */
    if (strcmp(table, "focallength_unknown") == 0)
        return tag == 0x0003U || tag == 0x0006U || tag == 0x0007U || tag == 0x0008U ||
               tag == 0x0009U || tag == 0x000CU || tag == 0x000EU || tag == 0x0010U ||
               tag == 0x0013U || tag == 0x0015U || tag == 0x001AU || tag == 0x001CU ||
               tag == 0x001EU || tag == 0x0023U || tag == 0x0028U || tag == 0x0038U ||
               tag == 0x0081U || tag == 0x0082U || tag == 0x0083U || tag == 0x0094U ||
               tag == 0x0095U || tag == 0x0096U || tag == 0x0097U || tag == 0x00A1U ||
               tag == 0x00A2U || tag == 0x00A3U || tag == 0x00A4U || tag == 0x00AEU ||
               tag == 0x00B2U || tag == 0x00B3U || tag == 0x00B4U || tag == 0x00D0U ||
               tag == 0x4002U || tag == 0x4005U || tag == 0x4008U || tag == 0x4009U ||
               tag == 0x4010U;
    if (strcmp(table, "focallength") == 0)
        return tag == 0x0000U || tag == 0x0001U || tag == 0x0002U || tag == 0x0003U;
    if (strcmp(table, "measuredcolor") == 0)
        return tag == 0x0001U;
    if (strcmp(table, "vignettingcorr") == 0)
        return tag == 0x0000U || tag == 0x0002U || tag == 0x0003U || tag == 0x0004U ||
               tag == 0x0005U || tag == 0x0006U || tag == 0x0009U || tag == 0x000BU ||
               tag == 0x000CU;
    return 0;
}
#endif
