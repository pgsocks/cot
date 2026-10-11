#include "allocation.h"
#include <stdlib.h>

void *cot_allocate(size_t size)
{
	/*
	 * Allocate size bytes with the library's system allocator.
	 * Return suitably aligned storage or NULL on allocation failure.
	 * Do not add mutable allocator configuration or throw exceptions.
	 * Keep this pair in a separate archive member so unit tests can
	 * supply deterministic allocation failures without public hooks.
	 */
	return malloc(size);
}

void cot_release(void *allocation)
{
	/* Release matching storage; NULL must be harmless. */
	free(allocation);
}
