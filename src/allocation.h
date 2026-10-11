#ifndef COT_ALLOCATION_H
#define COT_ALLOCATION_H

#include <stddef.h>

/* Private storage seam. All owned codec storage uses this matching pair. */
void *cot_allocate(size_t size);
void cot_release(void *allocation);

#endif
