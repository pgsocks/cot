#include <cot/cot.h>
#include <cot/cot.hpp>

int main(void)
{
	return cot_status_string(COT_STATUS_OK) ? 0 : 1;
}
