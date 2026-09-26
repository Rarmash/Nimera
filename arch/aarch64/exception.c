#include <nimera/exception.h>
#include <nimera/halt.h>

extern void exception_vectors(void);

void exception_init(void)
{
	u64 current_el;
	u64 vector_address = (u64)(unsigned long)exception_vectors;

	__asm__ volatile("mrs %0, currentel" : "=r"(current_el));
	current_el = (current_el >> 2) & 3ULL;

	if (current_el == 1ULL) {
		__asm__ volatile("msr vbar_el1, %0" :: "r"(vector_address)
					 : "memory");
	} else if (current_el == 2ULL) {
		__asm__ volatile("msr vbar_el2, %0" :: "r"(vector_address)
					 : "memory");
	}

	__asm__ volatile("isb" ::: "memory");
}

u64 exception_current_el(void)
{
	u64 current_el;

	__asm__ volatile("mrs %0, currentel" : "=r"(current_el));
	return (current_el >> 2) & 3ULL;
}

__attribute__((noreturn))
void exception_test_trigger(void)
{
	// 0xffffffff is not a valid AArch64 instruction and causes a synchronous
	// undefined-instruction exception without touching memory.
	__asm__ volatile(".inst 0xffffffff");
	cpu_halt();
}
