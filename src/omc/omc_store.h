#ifndef OMC_STORE_H
#define OMC_STORE_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_key.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_val.h"

OMC_EXTERN_C_BEGIN

typedef omc_u32 omc_block_id;
typedef omc_u32 omc_entry_id;

#define OMC_INVALID_BLOCK_ID ((omc_block_id)~(omc_block_id)0)
#define OMC_INVALID_ENTRY_ID ((omc_entry_id)~(omc_entry_id)0)

typedef enum omc_wire_family {
    OMC_WIRE_NONE = 0,
    OMC_WIRE_TIFF = 1,
    OMC_WIRE_OTHER = 2
} omc_wire_family;

typedef struct omc_wire_type {
    omc_wire_family family;
    omc_u16 code;
} omc_wire_type;

typedef struct omc_origin {
    omc_block_id block;
    omc_u32 order_in_block;
    omc_wire_type wire_type;
    omc_u32 wire_count;
    omc_byte_ref wire_type_name;
} omc_origin;

typedef omc_u32 omc_entry_flags;

#define OMC_ENTRY_FLAG_NONE ((omc_entry_flags)0U)
#define OMC_ENTRY_FLAG_DERIVED ((omc_entry_flags)1U)
#define OMC_ENTRY_FLAG_TRUNCATED ((omc_entry_flags)2U)
#define OMC_ENTRY_FLAG_UNREADABLE ((omc_entry_flags)4U)
#define OMC_ENTRY_FLAG_DIRTY ((omc_entry_flags)8U)
#define OMC_ENTRY_FLAG_DELETED ((omc_entry_flags)16U)

typedef struct omc_entry {
    omc_key key;
    omc_val value;
    omc_origin origin;
    omc_entry_flags flags;
} omc_entry;

typedef omc_blk_ref omc_block_info;

typedef struct omc_store {
    omc_arena arena;
    omc_entry* entries;
    omc_size entry_count;
    omc_size entry_capacity;
    omc_block_info* blocks;
    omc_size block_count;
    omc_size block_capacity;
} omc_store;

OMC_API void
omc_store_init(omc_store* store);

OMC_API void
omc_store_reset(omc_store* store);

OMC_API void
omc_store_fini(omc_store* store);

OMC_API omc_status
omc_store_reserve_entries(omc_store* store, omc_size capacity);

OMC_API omc_status
omc_store_reserve_blocks(omc_store* store, omc_size capacity);

OMC_API omc_status
omc_store_add_block(omc_store* store, const omc_block_info* info,
                    omc_block_id* out_id);

OMC_API omc_status
omc_store_add_entry(omc_store* store, const omc_entry* entry,
                    omc_entry_id* out_id);

OMC_API const omc_entry*
omc_store_entry(const omc_store* store, omc_entry_id id);

OMC_API const omc_block_info*
omc_store_block(const omc_store* store, omc_block_id id);

OMC_EXTERN_C_END

#endif
