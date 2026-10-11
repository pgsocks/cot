#include "allocation.h"
#include "support.h"

#include <atomic>
#include <cstdlib>

static std::atomic<size_t> outstanding{0};
static thread_local size_t allocation_index;
static thread_local size_t fail_index;

void *cot_allocate(size_t size)
{
	++allocation_index;
	if (fail_index && allocation_index == fail_index)
		return nullptr;
	void *allocation = std::malloc(size);

	if (allocation)
		++outstanding;
	return allocation;
}

void cot_release(void *allocation)
{
	if (!allocation)
		return;
	std::free(allocation);
	--outstanding;
}

namespace cot_test {

allocation_failure::allocation_failure(size_t index)
{
	allocation_index = 0;
	fail_index = index;
}

allocation_failure::~allocation_failure()
{
	fail_index = 0;
}

size_t allocation_failure::attempts() const
{
	return allocation_index;
}

size_t live_allocations()
{
	return outstanding.load();
}

}
