#include <nimera/types.h>

u64 arch_mmu_enabled(void)
{
	u64 system_control;

	// SCTLR_EL1.M is the MMU enable bit. The current heap uses identity
	// physical pointers and therefore requires this bit to remain clear.
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(system_control));
	return system_control & 1ULL;
}
