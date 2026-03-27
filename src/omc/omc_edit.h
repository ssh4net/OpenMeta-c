#ifndef OMC_EDIT_H
#define OMC_EDIT_H

#include "omc/omc_store.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_edit_op_kind {
    OMC_EDIT_OP_ADD_ENTRY = 0,
    OMC_EDIT_OP_SET_VALUE = 1,
    OMC_EDIT_OP_TOMBSTONE = 2
} omc_edit_op_kind;

typedef struct omc_edit_op {
    omc_edit_op_kind kind;
    omc_entry_id target;
    omc_entry entry;
    omc_val value;
} omc_edit_op;

typedef struct omc_edit {
    omc_arena arena;
    omc_edit_op* ops;
    omc_size op_count;
    omc_size op_capacity;
} omc_edit;

OMC_API void
omc_edit_init(omc_edit* edit);

OMC_API void
omc_edit_reset(omc_edit* edit);

OMC_API void
omc_edit_fini(omc_edit* edit);

OMC_API omc_status
omc_edit_reserve_ops(omc_edit* edit, omc_size capacity);

OMC_API omc_status
omc_edit_add_entry(omc_edit* edit, const omc_entry* entry);

OMC_API omc_status
omc_edit_set_value(omc_edit* edit, omc_entry_id target, const omc_val* value);

OMC_API omc_status
omc_edit_tombstone(omc_edit* edit, omc_entry_id target);

OMC_API omc_status
omc_edit_commit(const omc_store* base, const omc_edit* edits,
                omc_size edit_count, omc_store* out);

OMC_API omc_status
omc_store_compact(const omc_store* base, omc_store* out);

OMC_EXTERN_C_END

#endif
