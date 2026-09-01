#ifndef GUARD_MALLOC_H
#define GUARD_MALLOC_H

#include "global.h"

// POKEPVP (Execution Plan Phase 3, ADR-060): shrunk from the stock
// 0x1C000 by 0x1000 to carve out the EWRAM mailbox region reserved in
// ld_script.ld immediately before gHeap. The two values must move
// together -- ld_script.ld's own comment at the mailbox reservation
// cross-references this one. Stock EWRAM has zero free bytes (see
// ADR-060), so this is a real trade of heap headroom, not free space.
#define HEAP_SIZE 0x1B000
#define malloc Alloc
#define calloc(ct, sz) AllocZeroed((ct) * (sz))
#define free Free

#define FREE_AND_SET_NULL(ptr)          \
{                                       \
    free(ptr);                          \
    ptr = NULL;                         \
}

#define TRY_FREE_AND_SET_NULL(ptr) if (ptr != NULL) FREE_AND_SET_NULL(ptr)

extern u8 gHeap[];
void *Alloc(u32 size);
void *AllocZeroed(u32 size);
void Free(void *pointer);
void InitHeap(void *pointer, u32 size);

#endif // GUARD_MALLOC_H
