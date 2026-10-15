#ifndef OMC_NATIVE_EDIT_INTERNAL_H
#define OMC_NATIVE_EDIT_INTERNAL_H

#include "omc/omc_native_edit.h"

/* Directory indices: IFD0=0, ExifIFD=1, GPSIFD=2. */
int omc_native_edit_removal_allowed(unsigned directory, omc_u16 tag);

#endif
