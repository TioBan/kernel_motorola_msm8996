/*
 *  arch/arm/mach-integrator/include/mach/irqs.h
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* 
 *  Interrupt numbers
 */
#define IRQ_PIC_START			1
#define IRQ_SOFTINT			1
#define IRQ_UARTINT0			2
#define IRQ_UARTINT1			3
#define IRQ_KMIINT0			4
#define IRQ_KMIINT1			5
#define IRQ_TIMERINT0			6
#define IRQ_TIMERINT1			7
#define IRQ_TIMERINT2			8
#define IRQ_RTCINT			9
#define IRQ_AP_EXPINT0			10
#define IRQ_AP_EXPINT1			11
#define IRQ_AP_EXPINT2			12
#define IRQ_AP_EXPINT3			13
#define IRQ_AP_PCIINT0			14
#define IRQ_AP_PCIINT1			15
#define IRQ_AP_PCIINT2			16
#define IRQ_AP_PCIINT3			17
#define IRQ_AP_V3INT			18
#define IRQ_AP_CPINT0			19
#define IRQ_AP_CPINT1			20
#define IRQ_AP_LBUSTIMEOUT 		21
#define IRQ_AP_APCINT			22
#define IRQ_CP_CLCDCINT			23
#define IRQ_CP_MMCIINT0			24
#define IRQ_CP_MMCIINT1			25
#define IRQ_CP_AACIINT			26
#define IRQ_CP_CPPLDINT			27
#define IRQ_CP_ETHINT			28
#define IRQ_CP_TSPENINT			29
#define IRQ_PIC_END			29

#define IRQ_CIC_START			32
#define IRQ_CM_SOFTINT			32
#define IRQ_CM_COMMRX			33
#define IRQ_CM_COMMTX			34
#define IRQ_CIC_END			34

/*
 * IntegratorCP only
 */
#define IRQ_SIC_START			35
#define IRQ_SIC_CP_SOFTINT		35
#define IRQ_SIC_CP_RI0			36
#define IRQ_SIC_CP_RI1			37
#define IRQ_SIC_CP_CARDIN		38
#define IRQ_SIC_CP_LMINT0		39
#define IRQ_SIC_CP_LMINT1		40
#define IRQ_SIC_CP_LMINT2		41
#define IRQ_SIC_CP_LMINT3		42
#define IRQ_SIC_CP_LMINT4		43
#define IRQ_SIC_CP_LMINT5		44
#define IRQ_SIC_CP_LMINT6		45
#define IRQ_SIC_CP_LMINT7		46
#define IRQ_SIC_END			46

#define NR_IRQS_INTEGRATOR_AP		34
#define NR_IRQS_INTEGRATOR_CP		47
