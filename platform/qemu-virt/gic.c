#include <nimera/irq.h>
#include <nimera/panic.h>

#define GICD_CTLR 0x000U
#define GICD_ISENABLER 0x100U
#define GICD_IPRIORITYR 0x400U
#define GICD_ICFGR 0xc00U
#define GICD_ITARGETSR 0x800U

#define GICC_CTLR 0x000U
#define GICC_PMR 0x004U
#define GICC_IAR 0x00cU
#define GICC_EOIR 0x010U

#define GIC_SPURIOUS_INTERRUPT 1023ULL
#define INTERRUPT_PRIORITY 0x80U

static volatile unsigned int *gicd;
static volatile unsigned int *gicc;

static void write8(volatile unsigned char *address, unsigned char value)
{
	*address = value;
}

static void enable_interrupt(u64 interrupt_id)
{
	volatile unsigned int *configuration;
	u64 shift;
	volatile unsigned char *target;

	if (interrupt_id >= 32ULL) {
		/* SPI target byte 0 selects CPU0; the DTB describes level-high. */
		target = (volatile unsigned char *)gicd + GICD_ITARGETSR + interrupt_id;
		*target = 1U;
		configuration = gicd + GICD_ICFGR / sizeof(unsigned int) +
			interrupt_id / 16ULL;
		shift = (interrupt_id % 16ULL) * 2ULL;
		*configuration &= ~(1U << (shift + 1ULL));
	} else {
		/* The timer PPI is level-high; clear its edge bit. */
		configuration = gicd + GICD_ICFGR / sizeof(unsigned int) +
			interrupt_id / 16ULL;
		shift = (interrupt_id % 16ULL) * 2ULL;
		*configuration &= ~(1U << (shift + 1ULL));
	}
	write8((volatile unsigned char *)gicd + GICD_IPRIORITYR + interrupt_id,
	       INTERRUPT_PRIORITY);
	*(gicd + GICD_ISENABLER / sizeof(unsigned int) + interrupt_id / 32ULL) |=
		1U << (interrupt_id % 32ULL);
}

void platform_gic_enable_interrupt(u64 interrupt_id)
{
	if (gicd == (volatile unsigned int *)0 || interrupt_id == 0ULL) return;
	enable_interrupt(interrupt_id);
}

void platform_gic_init(const struct irq_platform_info *info)
{
	gicd = (volatile unsigned int *)(unsigned long)info->gic_distributor_base;
	gicc = (volatile unsigned int *)(unsigned long)info->gic_cpu_base;

	/* Configure the one PPI before enabling either GIC interface. */
	*gicd = 0U;
	enable_interrupt(info->timer_intid);
	enable_interrupt(info->uart_intid);
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
