#ifndef OMC_DATETIME_INTERNAL_H
#define OMC_DATETIME_INTERNAL_H
#include "omc/omc_store.h"

typedef struct omc_datetime {
    char date[11];
    char time[9];
    char fraction[10];
    char offset[7];
} omc_datetime;

int omc_datetime_parse(const omc_u8 *bytes, omc_size size, omc_datetime *out);
int omc_iptc_project_datetime(const omc_store *store, omc_u16 date_dataset, char *out,
                              omc_size *size, omc_entry_id *date_entry);
#endif
