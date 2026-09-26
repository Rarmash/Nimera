#include <nimera/irq.h>
#include <nimera/panic.h>

#define GICD_CTLR 0x000U
#define GICD_ISENABLER 0x100U
#define GICD_IPRIORITYR 0x400U
#define GICD_ICFGR 0xc00U

#define GICC_CTLR 0x000U
#define GICC_PMR 0x004U
#define GICC_IAR 0x00cU
#define GICC_EOIR 0x010U

#define GIC_SPURIOUS_INTERRUPT 1023ULL
#define TIMER_PRIORITY 0x80U

static volatile unsigned int *gicd;
static volatile unsigned int *gicc;

static void write8(volatile unsigned char *address, unsigned char value)
{
	*address = value;
}

static void enable_timer_interrupt(u64 interrupt_id)
{
	volatile unsigned int *enable;
	volatile unsigned int *configuration;
	u64 shift;

	if (interrupt_id >= 32ULL) {
		panic("timer interrupt is not a PPI");
	}
	enable = gicd + GICD_ISENABLER / sizeof(unsigned int);
	configuration =
		gicd + GICD_ICFGR / sizeof(unsigned int) + interrupt_id / 16ULL;
	shift = (interrupt_id % 16ULL) * 2ULL;
	/* The DTB describes the timer as level-high; clear the edge bit. */
	*configuration &= ~(1U << (shift + 1ULL));
	write8((volatile unsigned char *)gicd + GICD_IPRIORITYR + interrupt_id,
	       TIMER_PRIORITY);
	*enable |= 1U << interrupt_id;
}

void platform_gic_init(const struct irq_platform_info *info)
{
	gicd = (volatile unsigned int *)(unsigned long)info->gic_distributor_base;
	gicc = (volatile unsigned int *)(unsigned long)info->gic_cpu_base;

	/* Configure the one PPI before enabling either GIC interface. */
	*gicd = 0U;
	enable_timer_interrupt(info->timer_intid);
	gicc[GICC_PMR / sizeof(unsigned int)] = 0xffU;
	gicc[GICC_CTLR / sizeof(unsigned int)] = 1U;
	gicd[GICD_CTLR / sizeof(unsigned int)] = 1U;
}

u64 platform_gic_acknowledge(void)
{
	return (u64)(gicc[GICC_IAR / sizeof(unsigned int)] & 0x3ffU);
}

void platform_gic_end(u64 interrupt_id)
{
	if (interrupt_id == GIC_SPURIOUS_INTERRUPT) {
		return;
	}
	gicc[GICC_EOIR / sizeof(unsigned int)] = (unsigned int)interrupt_id;
}
