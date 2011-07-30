#ifndef __MACH_MX51_H__
#define __MACH_MX51_H__

/*
 * IROM
 */
#define MX51_IROM_BASE_ADDR		0x0
#define MX51_IROM_SIZE			SZ_64K

/*
 * IRAM
 */
#define MX51_IRAM_BASE_ADDR		0x1ffe0000	/* internal ram */
#define MX51_IRAM_PARTITIONS		16
#define MX51_IRAM_SIZE		(MX51_IRAM_PARTITIONS * SZ_8K)	/* 128KB */

#define MX51_GPU_BASE_ADDR		0x20000000
#define MX51_GPU_CTRL_BASE_ADDR		0x30000000
#define MX51_IPU_CTRL_BASE_ADDR		0x40000000

#define MX51_DEBUG_BASE_ADDR		0x60000000
#define MX51_DEBUG_SIZE			SZ_1M

#define MX51_ETB_BASE_ADDR		(MX51_DEBUG_BASE_ADDR + 0x01000)
#define MX51_ETM_BASE_ADDR		(MX51_DEBUG_BASE_ADDR + 0x02000)
#define MX51_TPIU_BASE_ADDR		(MX51_DEBUG_BASE_ADDR + 0x03000)
#define MX51_CTI0_BASE_ADDR		(MX51_DEBUG_BASE_ADDR + 0x04000)
#define MX51_CTI1_BASE_ADDR		(MX51_DEBUG_BASE_ADDR + 0x05000)
#define MX51_CTI2_BASE_ADDR		(MX51_DEBUG_BASE_ADDR + 0x06000)
#define MX51_CTI3_BASE_ADDR		(MX51_DEBUG_BASE_ADDR + 0x07000)
#define MX51_CORTEX_DBG_BASE_ADDR	(MX51_DEBUG_BASE_ADDR + 0x08000)

/*
 * SPBA global module enabled #0
 */
#define MX51_SPBA0_BASE_ADDR		0x70000000
#define MX51_SPBA0_SIZE			SZ_1M

#define MX51_ESDHC1_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x04000)
#define MX51_ESDHC2_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x08000)
#define MX51_UART3_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x0c000)
#define MX51_ECSPI1_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x10000)
#define MX51_SSI2_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x14000)
#define MX51_ESDHC3_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x20000)
#define MX51_ESDHC4_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x24000)
#define MX51_SPDIF_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x28000)
#define MX51_ATA_DMA_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x30000)
#define MX51_SLIM_DMA_BASE_ADDR		(MX51_SPBA0_BASE_ADDR + 0x34000)
#define MX51_HSI2C_DMA_BASE_ADDR	(MX51_SPBA0_BASE_ADDR + 0x38000)
#define MX51_SPBA_CTRL_BASE_ADDR	(MX51_SPBA0_BASE_ADDR + 0x3c000)

/*
 * AIPS 1
 */
#define MX51_AIPS1_BASE_ADDR		0x73f00000
#define MX51_AIPS1_SIZE			SZ_1M

#define MX51_USB_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x80000)
#define MX51_USB_OTG_BASE_ADDR		(MX51_USB_BASE_ADDR + 0x0000)
#define MX51_USB_HS1_BASE_ADDR		(MX51_USB_BASE_ADDR + 0x0200)
#define MX51_USB_HS2_BASE_ADDR		(MX51_USB_BASE_ADDR + 0x0400)
#define MX51_GPIO1_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x84000)
#define MX51_GPIO2_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x88000)
#define MX51_GPIO3_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x8c000)
#define MX51_GPIO4_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x90000)
#define MX51_KPP_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x94000)
#define MX51_WDOG1_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x98000)
#define MX51_WDOG2_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0x9c000)
#define MX51_GPT1_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xa0000)
#define MX51_SRTC_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xa4000)
#define MX51_IOMUXC_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xa8000)
#define MX51_EPIT1_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xac000)
#define MX51_EPIT2_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xb0000)
#define MX51_PWM1_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xb4000)
#define MX51_PWM2_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xb8000)
#define MX51_UART1_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xbc000)
#define MX51_UART2_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xc0000)
#define MX51_SRC_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xd0000)
#define MX51_CCM_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xd4000)
#define MX51_GPC_BASE_ADDR		(MX51_AIPS1_BASE_ADDR + 0xd8000)

/*
 * AIPS 2
 */
#define MX51_AIPS2_BASE_ADDR		0x83f00000
#define MX51_AIPS2_SIZE			SZ_1M

#define MX51_PLL1_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0x80000)
#define MX51_PLL2_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0x84000)
#define MX51_PLL3_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0x88000)
#define MX51_AHBMAX_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0x94000)
#define MX51_IIM_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0x98000)
#define MX51_CSU_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0x9c000)
#define MX51_ARM_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xa0000)
#define MX51_OWIRE_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xa4000)
#define MX51_FIRI_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xa8000)
#define MX51_ECSPI2_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xac000)
#define MX51_SDMA_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xb0000)
#define MX51_SCC_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xb4000)
#define MX51_ROMCP_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xb8000)
#define MX51_RTIC_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xbc000)
#define MX51_CSPI_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xc0000)
#define MX51_I2C2_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xc4000)
#define MX51_I2C1_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xc8000)
#define MX51_SSI1_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xcc000)
#define MX51_AUDMUX_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xd0000)
#define MX51_M4IF_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xd8000)
#define MX51_ESDCTL_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xd9000)
#define MX51_WEIM_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xda000)
#define MX51_NFC_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xdb000)
#define MX51_EMI_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xdbf00)
#define MX51_MIPI_HSC_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xdc000)
#define MX51_ATA_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xe0000)
#define MX51_SIM_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xe4000)
#define MX51_SSI3_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xe8000)
#define MX51_FEC_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xec000)
#define MX51_TVE_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xf0000)
#define MX51_VPU_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xf4000)
#define MX51_SAHARA_BASE_ADDR		(MX51_AIPS2_BASE_ADDR + 0xf8000)

#define MX51_CSD0_BASE_ADDR		0x90000000
#define MX51_CSD1_BASE_ADDR		0xa0000000
#define MX51_CS0_BASE_ADDR		0xb0000000
#define MX51_CS1_BASE_ADDR		0xb8000000
#define MX51_CS2_BASE_ADDR		0xc0000000
#define MX51_CS3_BASE_ADDR		0xc8000000
#define MX51_CS4_BASE_ADDR		0xcc000000
#define MX51_CS5_BASE_ADDR		0xce000000

/*
 * NFC
 */
#define MX51_NFC_AXI_BASE_ADDR		0xcfff0000	/* NAND flash AXI */
#define MX51_NFC_AXI_SIZE		SZ_64K

#define MX51_GPU2D_BASE_ADDR		0xd0000000
#define MX51_TZIC_BASE_ADDR		0xe0000000

#define MX51_IO_P2V(x)			IMX_IO_P2V(x)
#define MX51_IO_ADDRESS(x)		IOMEM(MX51_IO_P2V(x))

/*
 * defines for SPBA modules
 */
#define MX51_SPBA_SDHC1	0x04
#define MX51_SPBA_SDHC2	0x08
#define MX51_SPBA_UART3	0x0c
#define MX51_SPBA_CSPI1	0x10
#define MX51_SPBA_SSI2	0x14
#define MX51_SPBA_SDHC3	0x20
#define MX51_SPBA_SDHC4	0x24
#define MX51_SPBA_SPDIF	0x28
#define MX51_SPBA_ATA	0x30
#define MX51_SPBA_SLIM	0x34
#define MX51_SPBA_HSI2C	0x38
#define MX51_SPBA_CTRL	0x3c

/*
 * Defines for modules using static and dynamic DMA channels
 */
#define MX51_MXC_DMA_CHANNEL_IRAM	30
#define MX51_MXC_DMA_CHANNEL_SPDIF_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_UART1_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_UART1_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_UART2_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_UART2_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_UART3_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_UART3_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_MMC1	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_MMC2	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_SSI1_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_SSI1_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_SSI2_RX	MXC_DMA_DYNAMIC_CHANNEL
#ifdef CONFIG_SDMA_IRAM
#define MX51_MXC_DMA_CHANNEL_SSI2_TX	(MX51_MXC_DMA_CHANNEL_IRAM + 1)
#else				/*CONFIG_SDMA_IRAM */
#define MX51_MXC_DMA_CHANNEL_SSI2_TX	MXC_DMA_DYNAMIC_CHANNEL
#endif				/*CONFIG_SDMA_IRAM */
#define MX51_MXC_DMA_CHANNEL_CSPI1_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_CSPI1_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_CSPI2_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_CSPI2_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_CSPI3_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_CSPI3_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_ATA_RX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_ATA_TX	MXC_DMA_DYNAMIC_CHANNEL
#define MX51_MXC_DMA_CHANNEL_MEMORY	MXC_DMA_DYNAMIC_CHANNEL

#define MX51_IS_MEM_DEVICE_NONSHARED(x)		0

/*
 * DMA request assignments
 */
#define MX51_DMA_REQ_VPU		0
#define MX51_DMA_REQ_GPC		1
#define MX51_DMA_REQ_ATA_RX		2
#define MX51_DMA_REQ_ATA_TX		3
#define MX51_DMA_REQ_ATA_TX_END		4
#define MX51_DMA_REQ_SLIM_B		5
#define MX51_DMA_REQ_CSPI1_RX		6
#define MX51_DMA_REQ_CSPI1_TX		7
#define MX51_DMA_REQ_CSPI2_RX		8
#define MX51_DMA_REQ_CSPI2_TX		9
#define MX51_DMA_REQ_HS_I2C_TX		10
#define MX51_DMA_REQ_HS_I2C_RX		11
#define MX51_DMA_REQ_FIRI_RX		12
#define MX51_DMA_REQ_FIRI_TX		13
#define MX51_DMA_REQ_EXTREQ1		14
#define MX51_DMA_REQ_GPU		15
#define MX51_DMA_REQ_UART2_RX		16
#define MX51_DMA_REQ_UART2_TX		17
#define MX51_DMA_REQ_UART1_RX		18
#define MX51_DMA_REQ_UART1_TX		19
#define MX51_DMA_REQ_SDHC1		20
#define MX51_DMA_REQ_SDHC2		21
#define MX51_DMA_REQ_SSI2_RX1		22
#define MX51_DMA_REQ_SSI2_TX1		23
#define MX51_DMA_REQ_SSI2_RX0		24
#define MX51_DMA_REQ_SSI2_TX0		25
#define MX51_DMA_REQ_SSI1_RX1		26
#define MX51_DMA_REQ_SSI1_TX1		27
#define MX51_DMA_REQ_SSI1_RX0		28
#define MX51_DMA_REQ_SSI1_TX0		29
#define MX51_DMA_REQ_EMI_RD		30
#define MX51_DMA_REQ_CTI2_0		31
#define MX51_DMA_REQ_EMI_WR		32
#define MX51_DMA_REQ_CTI2_1		33
#define MX51_DMA_REQ_EPIT2		34
#define MX51_DMA_REQ_SSI3_RX1		35
#define MX51_DMA_REQ_IPU		36
#define MX51_DMA_REQ_SSI3_TX1		37
#define MX51_DMA_REQ_CSPI_RX		38
#define MX51_DMA_REQ_CSPI_TX		39
#define MX51_DMA_REQ_SDHC3		40
#define MX51_DMA_REQ_SDHC4		41
#define MX51_DMA_REQ_SLIM_B_TX		42
#define MX51_DMA_REQ_UART3_RX		43
#define MX51_DMA_REQ_UART3_TX		44
#define MX51_DMA_REQ_SPDIF		45
#define MX51_DMA_REQ_SSI3_RX0		46
#define MX51_DMA_REQ_SSI3_TX0		47

/*
 * Interrupt numbers
 */
#define MX51_INT_BASE			0
#define MX51_INT_RESV0			0
#define MX51_INT_ESDHC1			1
#define MX51_INT_ESDHC2			2
#define MX51_INT_ESDHC3			3
#define MX51_INT_ESDHC4			4
#define MX51_INT_RESV5			5
#define MX51_INT_SDMA			6
#define MX51_INT_IOMUX			7
#define MX51_INT_NFC			8
#define MX51_INT_VPU			9
#define MX51_INT_IPU_ERR		10
#define MX51_INT_IPU_SYN		11
#define MX51_INT_GPU			12
#define MX51_INT_RESV13			13
#define MX51_INT_USB_HS1		14
#define MX51_INT_EMI			15
#define MX51_INT_USB_HS2		16
#define MX51_INT_USB_HS3		17
#define MX51_INT_USB_OTG		18
#define MX51_INT_SAHARA_H0		19
#define MX51_INT_SAHARA_H1		20
#define MX51_INT_SCC_SMN		21
#define MX51_INT_SCC_STZ		22
#define MX51_INT_SCC_SCM		23
#define MX51_INT_SRTC_NTZ		24
#define MX51_INT_SRTC_TZ		25
#define MX51_INT_RTIC			26
#define MX51_INT_CSU			27
#define MX51_INT_SLIM_B			28
#define MX51_INT_SSI1			29
#define MX51_INT_SSI2			30
#define MX51_INT_UART1			31
#define MX51_INT_UART2			32
#define MX51_INT_UART3			33
#define MX51_INT_RESV34			34
#define MX51_INT_RESV35			35
#define MX51_INT_ECSPI1			36
#define MX51_INT_ECSPI2			37
#define MX51_INT_CSPI			38
#define MX51_INT_GPT			39
#define MX51_INT_EPIT1			40
#define MX51_INT_EPIT2			41
#define MX51_INT_GPIO1_INT7		42
#define MX51_INT_GPIO1_INT6		43
#define MX51_INT_GPIO1_INT5		44
#define MX51_INT_GPIO1_INT4		45
#define MX51_INT_GPIO1_INT3		46
#define MX51_INT_GPIO1_INT2		47
#define MX51_INT_GPIO1_INT1		48
#define MX51_INT_GPIO1_INT0		49
#define MX51_INT_GPIO1_LOW		50
#define MX51_INT_GPIO1_HIGH		51
#define MX51_INT_GPIO2_LOW		52
#define MX51_INT_GPIO2_HIGH		53
#define MX51_INT_GPIO3_LOW		54
#define MX51_INT_GPIO3_HIGH		55
#define MX51_INT_GPIO4_LOW		56
#define MX51_INT_GPIO4_HIGH		57
#define MX51_INT_WDOG1			58
#define MX51_INT_WDOG2			59
#define MX51_INT_KPP			60
#define MX51_INT_PWM1			61
#define MX51_INT_I2C1			62
#define MX51_INT_I2C2			63
#define MX51_INT_HS_I2C			64
#define MX51_INT_RESV65			65
#define MX51_INT_RESV66			66
#define MX51_INT_SIM_IPB		67
#define MX51_INT_SIM_DAT		68
#define MX51_INT_IIM			69
#define MX51_INT_ATA			70
#define MX51_INT_CCM1			71
#define MX51_INT_CCM2			72
#define MX51_INT_GPC1				73
#define MX51_INT_GPC2			74
#define MX51_INT_SRC			75
#define MX51_INT_NM			76
#define MX51_INT_PMU			77
#define MX51_INT_CTI_IRQ		78
#define MX51_INT_CTI1_TG0		79
#define MX51_INT_CTI1_TG1		80
#define MX51_INT_MCG_ERR		81
#define MX51_INT_MCG_TMR		82
#define MX51_INT_MCG_FUNC		83
#define MX51_INT_GPU2_IRQ		84
#define MX51_INT_GPU2_BUSY		85
#define MX51_INT_RESV86			86
#define MX51_INT_FEC			87
#define MX51_INT_OWIRE			88
#define MX51_INT_CTI1_TG2		89
#define MX51_INT_SJC			90
#define MX51_INT_SPDIF			91
#define MX51_INT_TVE			92
#define MX51_INT_FIRI			93
#define MX51_INT_PWM2			94
#define MX51_INT_SLIM_EXP		95
#define MX51_INT_SSI3			96
#define MX51_INT_EMI_BOOT		97
#define MX51_INT_CTI1_TG3		98
#define MX51_INT_SMC_RX			99
#define MX51_INT_VPU_IDLE		100
#define MX51_INT_EMI_NFC		101
#define MX51_INT_GPU_IDLE		102

#if !defined(__ASSEMBLY__) && !defined(__MXC_BOOT_UNCOMPRESS)
extern int mx51_revision(void);
extern void mx51_display_revision(void);
#endif

/* tape-out 1 defines */
#define MX51_TZIC_BASE_ADDR_TO1		0x8fffc000

#endif	/* ifndef __MACH_MX51_H__ */
