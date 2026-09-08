/* SPDX-License-Identifier: Apache-2.0
 * Sony model selection follows OpenMeta C++ f11de3e0. */
#ifndef OMC_SONY_MODELS_H
#define OMC_SONY_MODELS_H
#include "omc/omc_base.h"
#include <string.h>
static int sony_model_matches_any(const char *model, const char *const *values,
                                  omc_size count)
{
    omc_size i;
    for (i = 0U; i < count; ++i)
        if (strcmp(model, values[i]) == 0)
            return 1;
    return 0;
}
static int sony_model_has_prefix_any(const char *model, const char *const *values,
                                     omc_size count)
{
    omc_size i;
    for (i = 0U; i < count; ++i)
        if (strncmp(model, values[i], strlen(values[i])) == 0)
            return 1;
    return 0;
}
static char select_sony_tag2010_variant(const char *model)
{
    static const char *const kTag2010bModels[] = {
        "SLT-A65", "SLT-A65V", "SLT-A77", "SLT-A77V", "NEX-7", "NEX-VG20E", "Lunar"};
    static const char *const kTag2010cModels[] = {"SLT-A37", "SLT-A57", "NEX-F3"};
    static const char *const kTag2010eModels[] = {
        "SLT-A99",   "SLT-A99V", "HV",       "SLT-A58", "ILCE-3000", "ILCE-3500",
        "NEX-3N",    "NEX-5R",   "NEX-5T",   "NEX-6",   "NEX-VG900", "NEX-VG30E",
        "DSC-RX100", "DSC-RX1",  "DSC-RX1R", "Stellar"};
    static const char *const kTag2010fModels[] = {"DSC-RX100M2", "DSC-QX10",
                                                  "DSC-QX100"};
    static const char *const kTag2010gModels[] = {
        "DSC-QX30",  "DSC-RX10",  "DSC-RX100M3", "DSC-HX60V", "DSC-HX350", "DSC-HX400V",
        "DSC-WX220", "DSC-WX350", "ILCE-7",      "ILCE-7R",   "ILCE-7S",   "ILCE-7M2",
        "ILCE-5000", "ILCE-5100", "ILCE-6000",   "ILCE-QX1",  "ILCA-68",   "ILCA-77M2"};
    static const char *const kTag2010hModels[] = {
        "DSC-HX80",   "DSC-HX90",   "DSC-HX90V",   "DSC-RX0",     "DSC-RX1RM2",
        "DSC-RX10M2", "DSC-RX10M3", "DSC-RX100M4", "DSC-RX100M5", "DSC-WX500",
        "ILCE-6300",  "ILCE-6500",  "ILCE-7RM2",   "ILCE-7SM2",   "ILCA-99M2"};
    static const char *const kTag2010iPrefixes[] = {
        "ILCE-6100",   "ILCE-6400",    "ILCE-6600",   "ILCE-7C",  "ILCE-7M3",
        "ILCE-7RM3",   "ILCE-7RM4",    "ILCE-9",      "ILCE-9M2", "DSC-RX10M4",
        "DSC-RX100M6", "DSC-RX100M5A", "DSC-RX100M7", "DSC-HX95", "DSC-HX99",
        "DSC-RX0M2",   "ZV-1",         "ZV-E10"};

    if (strcmp(model, "NEX-5N") == 0) {
        return 'A';
    }
    if (sony_model_matches_any(model, kTag2010bModels,
                               sizeof(kTag2010bModels) / sizeof(kTag2010bModels[0]))) {
        return 'B';
    }
    if (sony_model_matches_any(model, kTag2010cModels,
                               sizeof(kTag2010cModels) / sizeof(kTag2010cModels[0]))) {
        return 'C';
    }
    if (sony_model_matches_any(model, kTag2010eModels,
                               sizeof(kTag2010eModels) / sizeof(kTag2010eModels[0]))) {
        return 'E';
    }
    if (sony_model_matches_any(model, kTag2010fModels,
                               sizeof(kTag2010fModels) / sizeof(kTag2010fModels[0]))) {
        return 'F';
    }
    if (sony_model_matches_any(model, kTag2010gModels,
                               sizeof(kTag2010gModels) / sizeof(kTag2010gModels[0]))) {
        return 'G';
    }
    if (sony_model_matches_any(model, kTag2010hModels,
                               sizeof(kTag2010hModels) / sizeof(kTag2010hModels[0]))) {
        return 'H';
    }
    if (sony_model_has_prefix_any(model, kTag2010iPrefixes,
                                  sizeof(kTag2010iPrefixes) /
                                      sizeof(kTag2010iPrefixes[0]))) {
        return 'I';
    }
    return 'I';
}

static char select_sony_tag9050_variant(const char *model, int is_slt_family,
                                        int is_lunar)
{
    static const char *const kTag9050bModels[] = {
        "ILCE-6300", "ILCE-6500", "ILCA-99M2", "ILCE-6600", "ILCE-7C", "ILCE-7M3",
        "ILCE-7RM2", "ILCE-7SM2", "ILCE-9",    "ILCE-9M2",  "ZV-E10"};
    static const char *const kTag9050bPrefixes[] = {"ILCE-6100", "ILCE-6400",
                                                    "ILCE-7RM3", "ILCE-7RM4"};
    static const char *const kTag9050cModels[] = {
        "ILCE-7M4",
        "ILCE-7RM5",
        "ILCE-7SM3",
    };
    static const char *const kTag9050cPrefixes[] = {
        "ILCE-1",
        "ILME-FX3",
    };
    static const char *const kTag9050dModels[] = {
        "ILCE-6700", "ILCE-7CM2", "ILCE-7CR", "ILCE-7RM6",
        "ILME-FX2",  "ZV-E1",     "ZV-E10M2",
    };

    if (sony_model_matches_any(model, kTag9050dModels,
                               sizeof(kTag9050dModels) / sizeof(kTag9050dModels[0]))) {
        return 'D';
    }
    if (sony_model_matches_any(model, kTag9050cModels,
                               sizeof(kTag9050cModels) / sizeof(kTag9050cModels[0])) ||
        sony_model_has_prefix_any(model, kTag9050cPrefixes,
                                  sizeof(kTag9050cPrefixes) /
                                      sizeof(kTag9050cPrefixes[0]))) {
        return 'C';
    }
    if (sony_model_matches_any(model, kTag9050bModels,
                               sizeof(kTag9050bModels) / sizeof(kTag9050bModels[0])) ||
        sony_model_has_prefix_any(model, kTag9050bPrefixes,
                                  sizeof(kTag9050bPrefixes) /
                                      sizeof(kTag9050bPrefixes[0]))) {
        return 'B';
    }
    if (is_slt_family || is_lunar) {
        return 'A';
    }
    if (model[0] != 0 && strncmp(model, "DSC-", 4U) != 0 &&
        strcmp(model, "Stellar") != 0) {
        return 'A';
    }
    return 'B';
}

static char select_sony_tag9400_variant(const char *model, omc_u8 deciphered_v0,
                                        int is_legacy_slt_family, int is_lunar,
                                        int is_stellar)
{
    static const char *const kTag9400aModels[] = {
        "NEX-5R",    "NEX-5T",    "NEX-6",     "NEX-7",   "NEX-VG20E",
        "NEX-VG30E", "NEX-VG900", "DSC-RX100", "DSC-RX1", "DSC-RX1R",
    };
    static const char *const kTag9400bModels[] = {
        "SLT-A58",   "ILCE-3000",   "ILCE-3500", "NEX-3N",   "DSC-WX60",
        "DSC-WX300", "DSC-RX100M2", "DSC-HX50V", "DSC-QX10", "DSC-QX100",
    };

    if (sony_model_matches_any(model, kTag9400bModels,
                               sizeof(kTag9400bModels) / sizeof(kTag9400bModels[0]))) {
        return 'B';
    }
    if (is_lunar || is_stellar || is_legacy_slt_family ||
        sony_model_matches_any(model, kTag9400aModels,
                               sizeof(kTag9400aModels) / sizeof(kTag9400aModels[0]))) {
        return 'A';
    }
    if (deciphered_v0 == 0x0C) {
        return 'B';
    }
    return 'C';
}

static int sony_model_uses_tag9405a(const char *model, int is_legacy_slt_family,
                                    int is_lunar, int is_stellar)
{
    static const char *const kTag9405aDscModels[] = {
        "DSC-HX50V",   "DSC-HX300", "DSC-QX10",  "DSC-QX100", "DSC-RX100",
        "DSC-RX100M2", "DSC-RX1",   "DSC-RX1R",  "DSC-TX30",  "DSC-WX60",
        "DSC-WX80",    "DSC-WX200", "DSC-WX300",
    };

    if (is_legacy_slt_family || is_lunar || is_stellar ||
        strncmp(model, "NEX-", 4U) == 0 || strcmp(model, "ILCE-3000") == 0 ||
        strcmp(model, "ILCE-3500") == 0) {
        return 1;
    }
    return sony_model_matches_any(model, kTag9405aDscModels,
                                  sizeof(kTag9405aDscModels) /
                                      sizeof(kTag9405aDscModels[0]));
}

static omc_u16 sony_tag9405b_lens_zoom_tag(const char *model)
{
    static const char *const kTag034eModels[] = {
        "DSC-RX100M5", "DSC-RX100M5A", "DSC-RX100M6", "DSC-RX100M7", "DSC-RX10M4",
        "DSC-HX99",    "ILCE-6100",    "ILCE-6400",   "ILCE-6600",   "ILCE-7C",
        "ILCE-7M3",    "ILCE-9M2",     "ZV-E10",
    };
    static const char *const kTag034ePrefixes[] = {
        "ILCE-7RM3",
        "ILCE-7RM4",
    };
    static const char *const kTag035aModels[] = {
        "ILCE-7RM2",  "ILCE-7SM2",  "DSC-HX80",    "DSC-HX90V",
        "DSC-RX10M2", "DSC-RX10M3", "DSC-RX100M4", "DSC-WX500",
    };

    if (sony_model_matches_any(model, kTag035aModels,
                               sizeof(kTag035aModels) / sizeof(kTag035aModels[0]))) {
        return 0x035AU;
    }
    if (sony_model_matches_any(model, kTag034eModels,
                               sizeof(kTag034eModels) / sizeof(kTag034eModels[0])) ||
        sony_model_has_prefix_any(model, kTag034ePrefixes,
                                  sizeof(kTag034ePrefixes) /
                                      sizeof(kTag034ePrefixes[0]))) {
        return 0x034EU;
    }
    return 0x0342U;
}

static omc_u16 sony_tag9405b_vignetting_tag(const char *model)
{
    static const char *const kTag034aModels[] = {
        "ILCA-68", "ILCA-77M2", "ILCE-5000", "ILCE-5100", "ILCE-6000",
        "ILCE-7",  "ILCE-7R",   "ILCE-7S",   "ILCE-QX1",  "Lusso",
    };
    static const char *const kTag035cModels[] = {
        "ILCA-99M2", "ILCE-6100", "ILCE-6400", "ILCE-6500", "ILCE-6600",
        "ILCE-7C",   "ILCE-7M3",  "ILCE-9",    "ILCE-9M2",  "ZV-E10",
    };
    static const char *const kTag035cPrefixes[] = {
        "ILCE-7RM3",
        "ILCE-7RM4",
    };
    static const char *const kTag0368Models[] = {
        "ILCE-6300",
        "ILCE-7RM2",
        "ILCE-7SM2",
    };

    if (sony_model_matches_any(model, kTag034aModels,
                               sizeof(kTag034aModels) / sizeof(kTag034aModels[0]))) {
        return 0x034AU;
    }
    if (strcmp(model, "ILCE-7M2") == 0) {
        return 0x0350U;
    }
    if (sony_model_matches_any(model, kTag035cModels,
                               sizeof(kTag035cModels) / sizeof(kTag035cModels[0])) ||
        sony_model_has_prefix_any(model, kTag035cPrefixes,
                                  sizeof(kTag035cPrefixes) /
                                      sizeof(kTag035cPrefixes[0]))) {
        return 0x035CU;
    }
    if (sony_model_matches_any(model, kTag0368Models,
                               sizeof(kTag0368Models) / sizeof(kTag0368Models[0]))) {
        return 0x0368U;
    }
    return 0U;
}

static omc_u16 sony_tag9405b_chromatic_tag(const char *model)
{
    static const char *const kTag037cModels[] = {
        "ILCA-68", "ILCA-77M2", "ILCE-5000", "ILCE-5100", "ILCE-6000",
        "ILCE-7",  "ILCE-7R",   "ILCE-7S",   "ILCE-QX1",  "Lusso",
    };
    static const char *const kTag039cModels[] = {
        "ILCE-6300",
        "ILCE-7RM2",
        "ILCE-7SM2",
    };
    static const char *const kTag03b8Models[] = {
        "ILCE-6100", "ILCE-6400", "ILCE-6600", "ILCE-7C",
        "ILCE-7M3",  "ILCE-9",    "ILCE-9M2",  "ZV-E10",
    };
    static const char *const kTag03b8Prefixes[] = {
        "ILCE-7RM3",
        "ILCE-7RM4",
    };

    if (sony_model_matches_any(model, kTag037cModels,
                               sizeof(kTag037cModels) / sizeof(kTag037cModels[0]))) {
        return 0x037CU;
    }
    if (strcmp(model, "ILCE-7M2") == 0) {
        return 0x0384U;
    }
    if (sony_model_matches_any(model, kTag039cModels,
                               sizeof(kTag039cModels) / sizeof(kTag039cModels[0]))) {
        return 0x039CU;
    }
    if (strcmp(model, "ILCA-99M2") == 0 || strcmp(model, "ILCE-6500") == 0) {
        return 0x03B0U;
    }
    if (sony_model_matches_any(model, kTag03b8Models,
                               sizeof(kTag03b8Models) / sizeof(kTag03b8Models[0])) ||
        sony_model_has_prefix_any(model, kTag03b8Prefixes,
                                  sizeof(kTag03b8Prefixes) /
                                      sizeof(kTag03b8Prefixes[0]))) {
        return 0x03B8U;
    }
    return 0U;
}

static omc_u16 sony_tag9416_vignetting_tag(const char *model)
{
    if (strcmp(model, "ILCE-1") == 0 || strcmp(model, "ILCE-7SM3") == 0 ||
        strncmp(model, "ILME-FX3", 8U) == 0) {
        return 0x088FU;
    }
    if (strcmp(model, "ILCE-7M4") == 0) {
        return 0x0891U;
    }
    if (strcmp(model, "ILCE-7RM6") == 0) {
        return 0x0708U;
    }
    if (strcmp(model, "ILCE-1M2") == 0 || strcmp(model, "ILCE-6700") == 0 ||
        strcmp(model, "ILCE-7CM2") == 0 || strcmp(model, "ILCE-7CR") == 0 ||
        strcmp(model, "ILCE-7RM5") == 0 || strcmp(model, "ILME-FX2") == 0 ||
        strcmp(model, "ILME-FX30") == 0 || strcmp(model, "ZV-E1") == 0 ||
        strcmp(model, "ZV-E10M2") == 0) {
        return 0x089DU;
    }
    return 0U;
}

static omc_u16 sony_tag9416_apsc_tag(const char *model)
{
    if (strcmp(model, "ILCE-1") == 0 || strcmp(model, "ILCE-7SM3") == 0 ||
        strncmp(model, "ILME-FX3", 8U) == 0) {
        return 0x08B5U;
    }
    if (strcmp(model, "ILCE-7M4") == 0) {
        return 0x08B7U;
    }
    if (strcmp(model, "ILCE-7RM6") == 0) {
        return 0x074FU;
    }
    if (strcmp(model, "ILCE-1M2") == 0 || strcmp(model, "ILCE-7CM2") == 0 ||
        strcmp(model, "ILCE-7CR") == 0 || strcmp(model, "ILCE-7RM5") == 0 ||
        strcmp(model, "ZV-E1") == 0) {
        return 0x08E5U;
    }
    return 0U;
}

static omc_u16 sony_tag9416_chromatic_tag(const char *model)
{
    if (strcmp(model, "ILCE-1") == 0 || strcmp(model, "ILCE-7SM3") == 0 ||
        strncmp(model, "ILME-FX3", 8U) == 0) {
        return 0x0914U;
    }
    if (strcmp(model, "ILCE-7M4") == 0) {
        return 0x0916U;
    }
    if (strcmp(model, "ILCE-7RM6") == 0) {
        return 0x0843U;
    }
    if (strcmp(model, "ILCE-1M2") == 0 || strcmp(model, "ILCE-6700") == 0 ||
        strcmp(model, "ILCE-7CM2") == 0 || strcmp(model, "ILCE-7CR") == 0 ||
        strcmp(model, "ILCE-7RM5") == 0 || strcmp(model, "ILME-FX2") == 0 ||
        strcmp(model, "ILME-FX30") == 0 || strcmp(model, "ZV-E1") == 0 ||
        strcmp(model, "ZV-E10M2") == 0) {
        return 0x0945U;
    }
    return 0U;
}

static omc_u16 sony_tag2010h_meter_offset(const char *model)
{
    if (strcmp(model, "ILCA-99M2") == 0 || strcmp(model, "ILCE-6500") == 0 ||
        strcmp(model, "DSC-RX0") == 0 || strcmp(model, "DSC-RX100M5") == 0) {
        return 0x0398u;
    }
    return 0x0388u;
}

#endif
