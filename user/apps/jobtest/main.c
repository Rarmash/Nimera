#include "../../runtime/fsutil.h"

int main(void)
{
	/* Long enough for an interactive jobs/fg check, but finite and deterministic. */
	volatile unsigned long long work = 0ULL;
	if (nimera_terminal_clear() != NIMERA_NERR_ACCESS)
		nimera_print("jobtest: terminal control unexpectedly allowed\n");
	nimera_print("jobtest running\n");
	for (unsigned long long index = 0ULL; index < 250000000ULL; ++index)
		work += index;
	if (work == 0ULL) return 1;
	nimera_print("jobtest done\n");
	return 0;
}
