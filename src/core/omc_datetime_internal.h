#ifndef OMC_DATETIME_INTERNAL_H
#define OMC_DATETIME_INTERNAL_H
#include "omc/omc_store.h"

typedef struct omc_datetime {
    char date[11];
    char time[9];
    char fraction[10];
    char offset[7];
    omc_u8 precision;
} omc_datetime;

#define OMC_DATETIME_PRECISION_YEAR 1U
#define OMC_DATETIME_PRECISION_MONTH 2U
#define OMC_DATETIME_PRECISION_FULL 3U

int omc_datetime_parse(const omc_u8 *bytes, omc_size size, omc_datetime *out);
int omc_datetime_parse_creation(const omc_u8 *bytes, omc_size size,
                                omc_datetime *out);
int omc_iptc_project_datetime(const omc_store *store, omc_u16 date_dataset, char *out,
                              omc_size *size, omc_entry_id *date_entry);
#endif
