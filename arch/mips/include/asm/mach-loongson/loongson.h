/*
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON_LOONGSON_H
#define __ASM_MACH_LOONGSON_LOONGSON_H

#include <linux/io.h>
#include <linux/init.h>

/* loongson internal northbridge initialization */
extern void bonito_irq_init(void);

/* machine-specific reboot/halt operation */
extern void mach_prepare_reboot(void);
extern void mach_prepare_shutdown(void);

/* environment arguments from bootloader */
extern unsigned long cpu_clock_freq;
extern unsigned long memsize, highmemsize;

/* loongson-specific command line, env and memory initialization */
extern void __init prom_init_memory(void);
extern void __init prom_init_cmdline(void);
extern void __init prom_init_machtype(void);
extern void __init prom_init_env(void);
#ifdef CONFIG_LOONGSON_UART_BASE
extern unsigned long _loongson_uart_base, loongson_uart_base;
extern void prom_init_loongson_uart_base(void);
#endif

static inline void prom_init_uart_base(void)
{
#ifdef CONFIG_LOONGSON_UART_BASE
	prom_init_loongson_uart_base();
#endif
}

/* irq operation functions */
extern void bonito_irqdispatch(void);
extern void __init bonito_irq_init(void);
extern void __init mach_init_irq(void);
extern void mach_irq_dispatch(unsigned int pending);
extern int mach_i8259_irq(void);

/* We need this in some places... */
#define delay()	({		\
	int x;				\
	for (x = 0; x < 100000; x++)	\
		__asm__ __volatile__(""); \
})

#define LOONGSON_REG(x) \
	(*(volatile u32 *)((char *)CKSEG1ADDR(LOONGSON_REG_BASE) + (x)))

#define LOONGSON_IRQ_BASE	32
#define LOONGSON2_PERFCNT_IRQ	(MIPS_CPU_IRQ_BASE + 6) /* cpu perf counter */

#include <linux/interrupt.h>
static inline void do_perfcnt_IRQ(void)
{
#if defined(CONFIG_OPROFILE) || defined(CONFIG_OPROFILE_MODULE)
	do_IRQ(LOONGSON2_PERFCNT_IRQ);
#endif
}

#define LOONGSON_FLASH_BASE	0x1c000000
#define LOONGSON_FLASH_SIZE	0x02000000	/* 32M */
#define LOONGSON_FLASH_TOP	(LOONGSON_FLASH_BASE+LOONGSON_FLASH_SIZE-1)

#define LOONGSON_LIO0_BASE	0x1e000000
#define LOONGSON_LIO0_SIZE	0x01C00000	/* 28M */
#define LOONGSON_LIO0_TOP	(LOONGSON_LIO0_BASE+LOONGSON_LIO0_SIZE-1)

#define LOONGSON_BOOT_BASE	0x1fc00000
#define LOONGSON_BOOT_SIZE	0x00100000	/* 1M */
#define LOONGSON_BOOT_TOP 	(LOONGSON_BOOT_BASE+LOONGSON_BOOT_SIZE-1)
#define LOONGSON_REG_BASE 	0x1fe00000
#define LOONGSON_REG_SIZE 	0x00100000	/* 256Bytes + 256Bytes + ??? */
#define LOONGSON_REG_TOP	(LOONGSON_REG_BASE+LOONGSON_REG_SIZE-1)

#define LOONGSON_LIO1_BASE 	0x1ff00000
#define LOONGSON_LIO1_SIZE 	0x00100000	/* 1M */
#define LOONGSON_LIO1_TOP	(LOONGSON_LIO1_BASE+LOONGSON_LIO1_SIZE-1)

#define LOONGSON_PCILO0_BASE	0x10000000
#define LOONGSON_PCILO1_BASE	0x14000000
#define LOONGSON_PCILO2_BASE	0x18000000
#define LOONGSON_PCILO_BASE	LOONGSON_PCILO0_BASE
#define LOONGSON_PCILO_SIZE	0x0c000000	/* 64M * 3 */
#define LOONGSON_PCILO_TOP	(LOONGSON_PCILO0_BASE+LOONGSON_PCILO_SIZE-1)

#define LOONGSON_PCICFG_BASE	0x1fe80000
#define LOONGSON_PCICFG_SIZE	0x00000800	/* 2K */
#define LOONGSON_PCICFG_TOP	(LOONGSON_PCICFG_BASE+LOONGSON_PCICFG_SIZE-1)
#define LOONGSON_PCIIO_BASE	0x1fd00000
#define LOONGSON_PCIIO_SIZE	0x00100000	/* 1M */
#define LOONGSON_PCIIO_TOP	(LOONGSON_PCIIO_BASE+LOONGSON_PCIIO_SIZE-1)

/* Loongson Register Bases */

#define LOONGSON_PCICONFIGBASE	0x00
#define LOONGSON_REGBASE	0x100

/* PCI Configuration Registers */

#define LOONGSON_PCI_REG(x)	LOONGSON_REG(LOONGSON_PCICONFIGBASE + (x))
#define LOONGSON_PCIDID		LOONGSON_PCI_REG(0x00)
#define LOONGSON_PCICMD		LOONGSON_PCI_REG(0x04)
#define LOONGSON_PCICLASS 	LOONGSON_PCI_REG(0x08)
#define LOONGSON_PCILTIMER	LOONGSON_PCI_REG(0x0c)
#define LOONGSON_PCIBASE0 	LOONGSON_PCI_REG(0x10)
#define LOONGSON_PCIBASE1 	LOONGSON_PCI_REG(0x14)
#define LOONGSON_PCIBASE2 	LOONGSON_PCI_REG(0x18)
#define LOONGSON_PCIBASE3 	LOONGSON_PCI_REG(0x1c)
#define LOONGSON_PCIBASE4 	LOONGSON_PCI_REG(0x20)
#define LOONGSON_PCIEXPRBASE	LOONGSON_PCI_REG(0x30)
#define LOONGSON_PCIINT		LOONGSON_PCI_REG(0x3c)

#define LOONGSON_PCI_ISR4C	LOONGSON_PCI_REG(0x4c)

#define LOONGSON_PCICMD_PERR_CLR	0x80000000
#define LOONGSON_PCICMD_SERR_CLR	0x40000000
#define LOONGSON_PCICMD_MABORT_CLR	0x20000000
#define LOONGSON_PCICMD_MTABORT_CLR	0x10000000
#define LOONGSON_PCICMD_TABORT_CLR	0x08000000
#define LOONGSON_PCICMD_MPERR_CLR 	0x01000000
#define LOONGSON_PCICMD_PERRRESPEN	0x00000040
#define LOONGSON_PCICMD_ASTEPEN		0x00000080
#define LOONGSON_PCICMD_SERREN		0x00000100
#define LOONGSON_PCILTIMER_BUSLATENCY	0x0000ff00
#define LOONGSON_PCILTIMER_BUSLATENCY_SHIFT	8

/* Loongson h/w Configuration */

#define LOONGSON_GENCFG_OFFSET		0x4
#define LOONGSON_GENCFG	LOONGSON_REG(LOONGSON_REGBASE + LOONGSON_GENCFG_OFFSET)

#define LOONGSON_GENCFG_DEBUGMODE	0x00000001
#define LOONGSON_GENCFG_SNOOPEN		0x00000002
#define LOONGSON_GENCFG_CPUSELFRESET	0x00000004

#define LOONGSON_GENCFG_FORCE_IRQA	0x00000008
#define LOONGSON_GENCFG_IRQA_ISOUT	0x00000010
#define LOONGSON_GENCFG_IRQA_FROM_INT1	0x00000020
#define LOONGSON_GENCFG_BYTESWAP	0x00000040

#define LOONGSON_GENCFG_UNCACHED	0x00000080
#define LOONGSON_GENCFG_PREFETCHEN	0x00000100
#define LOONGSON_GENCFG_WBEHINDEN	0x00000200
#define LOONGSON_GENCFG_CACHEALG	0x00000c00
#define LOONGSON_GENCFG_CACHEALG_SHIFT	10
#define LOONGSON_GENCFG_PCIQUEUE	0x00001000
#define LOONGSON_GENCFG_CACHESTOP	0x00002000
#define LOONGSON_GENCFG_MSTRBYTESWAP	0x00004000
#define LOONGSON_GENCFG_BUSERREN	0x00008000
#define LOONGSON_GENCFG_NORETRYTIMEOUT	0x00010000
#define LOONGSON_GENCFG_SHORTCOPYTIMEOUT	0x00020000

/* PCI address map control */

#define LOONGSON_PCIMAP			LOONGSON_REG(LOONGSON_REGBASE + 0x10)
#define LOONGSON_PCIMEMBASECFG		LOONGSON_REG(LOONGSON_REGBASE + 0x14)
#define LOONGSON_PCIMAP_CFG		LOONGSON_REG(LOONGSON_REGBASE + 0x18)

/* GPIO Regs - r/w */

#define LOONGSON_GPIODATA 		LOONGSON_REG(LOONGSON_REGBASE + 0x1c)
#define LOONGSON_GPIOIE			LOONGSON_REG(LOONGSON_REGBASE + 0x20)

/* ICU Configuration Regs - r/w */

#define LOONGSON_INTEDGE		LOONGSON_REG(LOONGSON_REGBASE + 0x24)
#define LOONGSON_INTSTEER 		LOONGSON_REG(LOONGSON_REGBASE + 0x28)
#define LOONGSON_INTPOL			LOONGSON_REG(LOONGSON_REGBASE + 0x2c)

/* ICU Enable Regs - IntEn & IntISR are r/o. */

#define LOONGSON_INTENSET 		LOONGSON_REG(LOONGSON_REGBASE + 0x30)
#define LOONGSON_INTENCLR 		LOONGSON_REG(LOONGSON_REGBASE + 0x34)
#define LOONGSON_INTEN			LOONGSON_REG(LOONGSON_REGBASE + 0x38)
#define LOONGSON_INTISR			LOONGSON_REG(LOONGSON_REGBASE + 0x3c)

/* ICU */
#define LOONGSON_ICU_MBOXES		0x0000000f
#define LOONGSON_ICU_MBOXES_SHIFT 	0
#define LOONGSON_ICU_DMARDY		0x00000010
#define LOONGSON_ICU_DMAEMPTY		0x00000020
#define LOONGSON_ICU_COPYRDY		0x00000040
#define LOONGSON_ICU_COPYEMPTY		0x00000080
#define LOONGSON_ICU_COPYERR		0x00000100
#define LOONGSON_ICU_PCIIRQ		0x00000200
#define LOONGSON_ICU_MASTERERR		0x00000400
#define LOONGSON_ICU_SYSTEMERR		0x00000800
#define LOONGSON_ICU_DRAMPERR		0x00001000
#define LOONGSON_ICU_RETRYERR		0x00002000
#define LOONGSON_ICU_GPIOS		0x01ff0000
#define LOONGSON_ICU_GPIOS_SHIFT		16
#define LOONGSON_ICU_GPINS		0x7e000000
#define LOONGSON_ICU_GPINS_SHIFT		25
#define LOONGSON_ICU_MBOX(N)		(1<<(LOONGSON_ICU_MBOXES_SHIFT+(N)))
#define LOONGSON_ICU_GPIO(N)		(1<<(LOONGSON_ICU_GPIOS_SHIFT+(N)))
#define LOONGSON_ICU_GPIN(N)		(1<<(LOONGSON_ICU_GPINS_SHIFT+(N)))

/* PCI prefetch window base & mask */

#define LOONGSON_MEM_WIN_BASE_L 	LOONGSON_REG(LOONGSON_REGBASE + 0x40)
#define LOONGSON_MEM_WIN_BASE_H 	LOONGSON_REG(LOONGSON_REGBASE + 0x44)
#define LOONGSON_MEM_WIN_MASK_L 	LOONGSON_REG(LOONGSON_REGBASE + 0x48)
#define LOONGSON_MEM_WIN_MASK_H 	LOONGSON_REG(LOONGSON_REGBASE + 0x4c)

/* PCI_Hit*_Sel_* */

#define LOONGSON_PCI_HIT0_SEL_L		LOONGSON_REG(LOONGSON_REGBASE + 0x50)
#define LOONGSON_PCI_HIT0_SEL_H		LOONGSON_REG(LOONGSON_REGBASE + 0x54)
#define LOONGSON_PCI_HIT1_SEL_L		LOONGSON_REG(LOONGSON_REGBASE + 0x58)
#define LOONGSON_PCI_HIT1_SEL_H		LOONGSON_REG(LOONGSON_REGBASE + 0x5c)
#define LOONGSON_PCI_HIT2_SEL_L		LOONGSON_REG(LOONGSON_REGBASE + 0x60)
#define LOONGSON_PCI_HIT2_SEL_H		LOONGSON_REG(LOONGSON_REGBASE + 0x64)

/* PXArb Config & Status */

#define LOONGSON_PXARB_CFG		LOONGSON_REG(LOONGSON_REGBASE + 0x68)
#define LOONGSON_PXARB_STATUS		LOONGSON_REG(LOONGSON_REGBASE + 0x6c)

/* pcimap */

#define LOONGSON_PCIMAP_PCIMAP_LO0	0x0000003f
#define LOONGSON_PCIMAP_PCIMAP_LO0_SHIFT	0
#define LOONGSON_PCIMAP_PCIMAP_LO1	0x00000fc0
#define LOONGSON_PCIMAP_PCIMAP_LO1_SHIFT	6
#define LOONGSON_PCIMAP_PCIMAP_LO2	0x0003f000
#define LOONGSON_PCIMAP_PCIMAP_LO2_SHIFT	12
#define LOONGSON_PCIMAP_PCIMAP_2	0x00040000
#define LOONGSON_PCIMAP_WIN(WIN, ADDR)	\
	((((ADDR)>>26) & LOONGSON_PCIMAP_PCIMAP_LO0) << ((WIN)*6))

#ifdef CONFIG_CPU_SUPPORTS_CPUFREQ
#include <linux/cpufreq.h>
extern void loongson2_cpu_wait(void);
extern struct cpufreq_frequency_table loongson2_clockmod_table[];

/* Chip Config */
#define LOONGSON_CHIPCFG0		LOONGSON_REG(LOONGSON_REGBASE + 0x80)
#endif

/*
 * address windows configuration module
 *
 * loongson2e do not have this module
 */
#ifdef CONFIG_CPU_SUPPORTS_ADDRWINCFG

/* address window config module base address */
#define LOONGSON_ADDRWINCFG_BASE		0x3ff00000ul
#define LOONGSON_ADDRWINCFG_SIZE		0x180

extern unsigned long _loongson_addrwincfg_base;
#define LOONGSON_ADDRWINCFG(offset) \
	(*(volatile u64 *)(_loongson_addrwincfg_base + (offset)))

#define CPU_WIN0_BASE	LOONGSON_ADDRWINCFG(0x00)
#define CPU_WIN1_BASE	LOONGSON_ADDRWINCFG(0x08)
#define CPU_WIN2_BASE	LOONGSON_ADDRWINCFG(0x10)
#define CPU_WIN3_BASE	LOONGSON_ADDRWINCFG(0x18)

#define CPU_WIN0_MASK	LOONGSON_ADDRWINCFG(0x20)
#define CPU_WIN1_MASK	LOONGSON_ADDRWINCFG(0x28)
#define CPU_WIN2_MASK	LOONGSON_ADDRWINCFG(0x30)
#define CPU_WIN3_MASK	LOONGSON_ADDRWINCFG(0x38)

#define CPU_WIN0_MMAP	LOONGSON_ADDRWINCFG(0x40)
#define CPU_WIN1_MMAP	LOONGSON_ADDRWINCFG(0x48)
#define CPU_WIN2_MMAP	LOONGSON_ADDRWINCFG(0x50)
#define CPU_WIN3_MMAP	LOONGSON_ADDRWINCFG(0x58)

#define PCIDMA_WIN0_BASE	LOONGSON_ADDRWINCFG(0x60)
#define PCIDMA_WIN1_BASE	LOONGSON_ADDRWINCFG(0x68)
#define PCIDMA_WIN2_BASE	LOONGSON_ADDRWINCFG(0x70)
#define PCIDMA_WIN3_BASE	LOONGSON_ADDRWINCFG(0x78)

#define PCIDMA_WIN0_MASK	LOONGSON_ADDRWINCFG(0x80)
#define PCIDMA_WIN1_MASK	LOONGSON_ADDRWINCFG(0x88)
#define PCIDMA_WIN2_MASK	LOONGSON_ADDRWINCFG(0x90)
#define PCIDMA_WIN3_MASK	LOONGSON_ADDRWINCFG(0x98)

#define PCIDMA_WIN0_MMAP	LOONGSON_ADDRWINCFG(0xa0)
#define PCIDMA_WIN1_MMAP	LOONGSON_ADDRWINCFG(0xa8)
#define PCIDMA_WIN2_MMAP	LOONGSON_ADDRWINCFG(0xb0)
#define PCIDMA_WIN3_MMAP	LOONGSON_ADDRWINCFG(0xb8)

#define ADDRWIN_WIN0	0
#define ADDRWIN_WIN1	1
#define ADDRWIN_WIN2	2
#define ADDRWIN_WIN3	3

#define ADDRWIN_MAP_DST_DDR	0
#define ADDRWIN_MAP_DST_PCI	1
#define ADDRWIN_MAP_DST_LIO	1

/*
 * s: CPU, PCIDMA
 * d: DDR, PCI, LIO
 * win: 0, 1, 2, 3
 * src: map source
 * dst: map destination
 * size: ~mask + 1
 */
#define LOONGSON_ADDRWIN_CFG(s, d, w, src, dst, size) do {\
	s##_WIN##w##_BASE = (src); \
	s##_WIN##w##_MMAP = (dst) | ADDRWIN_MAP_DST_##d; \
	s##_WIN##w##_MASK = ~(size-1); \
} while (0)

#define LOONGSON_ADDRWIN_CPUTOPCI(win, src, dst, size) \
	LOONGSON_ADDRWIN_CFG(CPU, PCI, win, src, dst, size)
#define LOONGSON_ADDRWIN_CPUTODDR(win, src, dst, size) \
	LOONGSON_ADDRWIN_CFG(CPU, DDR, win, src, dst, size)
#define LOONGSON_ADDRWIN_PCITODDR(win, src, dst, size) \
	LOONGSON_ADDRWIN_CFG(PCIDMA, DDR, win, src, dst, size)

#endif	/* ! CONFIG_CPU_SUPPORTS_ADDRWINCFG */

#endif /* __ASM_MACH_LOONGSON_LOONGSON_H */
