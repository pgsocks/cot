#include <cot/cot.h>

int main(void)
{
	return cot_status_string(COT_STATUS_OK) ? 0 : 1;
}
