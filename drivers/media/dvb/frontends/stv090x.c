/*
	STV0900/0903 Multistandard Broadcast Frontend driver
	Copyright (C) Manu Abraham <abraham.manu@gmail.com>

	Copyright (C) ST Microelectronics

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/mutex.h>

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

#include "stv6110x.h" /* for demodulator internal modes */

#include "stv090x_reg.h"
#include "stv090x.h"
#include "stv090x_priv.h"

static unsigned int verbose;
module_param(verbose, int, 0644);

struct mutex demod_lock;

/* DVBS1 and DSS C/N Lookup table */
static const struct stv090x_tab stv090x_s1cn_tab[] = {
	{   0, 8917 }, /*  0.0dB */
	{   5, 8801 }, /*  0.5dB */
	{  10, 8667 }, /*  1.0dB */
	{  15, 8522 }, /*  1.5dB */
	{  20, 8355 }, /*  2.0dB */
	{  25, 8175 }, /*  2.5dB */
	{  30, 7979 }, /*  3.0dB */
	{  35, 7763 }, /*  3.5dB */
	{  40, 7530 }, /*  4.0dB */
	{  45, 7282 }, /*  4.5dB */
	{  50, 7026 }, /*  5.0dB */
	{  55, 6781 }, /*  5.5dB */
	{  60, 6514 }, /*  6.0dB */
	{  65, 6241 }, /*  6.5dB */
	{  70, 5965 }, /*  7.0dB */
	{  75, 5690 }, /*  7.5dB */
	{  80, 5424 }, /*  8.0dB */
	{  85, 5161 }, /*  8.5dB */
	{  90, 4902 }, /*  9.0dB */
	{  95, 4654 }, /*  9.5dB */
	{ 100, 4417 }, /* 10.0dB */
	{ 105, 4186 }, /* 10.5dB */
	{ 110, 3968 }, /* 11.0dB */
	{ 115, 3757 }, /* 11.5dB */
	{ 120, 3558 }, /* 12.0dB */
	{ 125, 3366 }, /* 12.5dB */
	{ 130, 3185 }, /* 13.0dB */
	{ 135, 3012 }, /* 13.5dB */
	{ 140, 2850 }, /* 14.0dB */
	{ 145, 2698 }, /* 14.5dB */
	{ 150, 2550 }, /* 15.0dB */
	{ 160, 2283 }, /* 16.0dB */
	{ 170, 2042 }, /* 17.0dB */
	{ 180, 1827 }, /* 18.0dB */
	{ 190, 1636 }, /* 19.0dB */
	{ 200, 1466 }, /* 20.0dB */
	{ 210, 1315 }, /* 21.0dB */
	{ 220, 1181 }, /* 22.0dB */
	{ 230, 1064 }, /* 23.0dB */
	{ 240,	960 }, /* 24.0dB */
	{ 250,	869 }, /* 25.0dB */
	{ 260,	792 }, /* 26.0dB */
	{ 270,	724 }, /* 27.0dB */
	{ 280,	665 }, /* 28.0dB */
	{ 290,	616 }, /* 29.0dB */
	{ 300,	573 }, /* 30.0dB */
	{ 310,	537 }, /* 31.0dB */
	{ 320,	507 }, /* 32.0dB */
	{ 330,	483 }, /* 33.0dB */
	{ 400,	398 }, /* 40.0dB */
	{ 450,	381 }, /* 45.0dB */
	{ 500,	377 }  /* 50.0dB */
};

/* DVBS2 C/N Lookup table */
static const struct stv090x_tab stv090x_s2cn_tab[] = {
	{ -30, 13348 }, /* -3.0dB */
	{ -20, 12640 }, /* -2d.0B */
	{ -10, 11883 }, /* -1.0dB */
	{   0, 11101 }, /* -0.0dB */
	{   5, 10718 }, /*  0.5dB */
	{  10, 10339 }, /*  1.0dB */
	{  15,  9947 }, /*  1.5dB */
	{  20,  9552 }, /*  2.0dB */
	{  25,  9183 }, /*  2.5dB */
	{  30,  8799 }, /*  3.0dB */
	{  35,  8422 }, /*  3.5dB */
	{  40,  8062 }, /*  4.0dB */
	{  45,  7707 }, /*  4.5dB */
	{  50,  7353 }, /*  5.0dB */
	{  55,  7025 }, /*  5.5dB */
	{  60,  6684 }, /*  6.0dB */
	{  65,  6331 }, /*  6.5dB */
	{  70,  6036 }, /*  7.0dB */
	{  75,  5727 }, /*  7.5dB */
	{  80,  5437 }, /*  8.0dB */
	{  85,  5164 }, /*  8.5dB */
	{  90,  4902 }, /*  9.0dB */
	{  95,  4653 }, /*  9.5dB */
	{ 100,  4408 }, /* 10.0dB */
	{ 105,  4187 }, /* 10.5dB */
	{ 110,  3961 }, /* 11.0dB */
	{ 115,  3751 }, /* 11.5dB */
	{ 120,  3558 }, /* 12.0dB */
	{ 125,  3368 }, /* 12.5dB */
	{ 130,  3191 }, /* 13.0dB */
	{ 135,  3017 }, /* 13.5dB */
	{ 140,  2862 }, /* 14.0dB */
	{ 145,  2710 }, /* 14.5dB */
	{ 150,  2565 }, /* 15.0dB */
	{ 160,  2300 }, /* 16.0dB */
	{ 170,  2058 }, /* 17.0dB */
	{ 180,  1849 }, /* 18.0dB */
	{ 190,  1663 }, /* 19.0dB */
	{ 200,  1495 }, /* 20.0dB */
	{ 210,  1349 }, /* 21.0dB */
	{ 220,  1222 }, /* 22.0dB */
	{ 230,  1110 }, /* 23.0dB */
	{ 240,  1011 }, /* 24.0dB */
	{ 250,   925 }, /* 25.0dB */
	{ 260,   853 }, /* 26.0dB */
	{ 270,   789 }, /* 27.0dB */
	{ 280,   734 }, /* 28.0dB */
	{ 290,   690 }, /* 29.0dB */
	{ 300,   650 }, /* 30.0dB */
	{ 310,   619 }, /* 31.0dB */
	{ 320,   593 }, /* 32.0dB */
	{ 330,   571 }, /* 33.0dB */
	{ 400,   498 }, /* 40.0dB */
	{ 450,	 484 }, /* 45.0dB */
	{ 500,	 481 }	/* 50.0dB */
};

/* RF level C/N lookup table */
static const struct stv090x_tab stv090x_rf_tab[] = {
	{  -5, 0xcaa1 }, /*  -5dBm */
	{ -10, 0xc229 }, /* -10dBm */
	{ -15, 0xbb08 }, /* -15dBm */
	{ -20, 0xb4bc }, /* -20dBm */
	{ -25, 0xad5a }, /* -25dBm */
	{ -30, 0xa298 }, /* -30dBm */
	{ -35, 0x98a8 }, /* -35dBm */
	{ -40, 0x8389 }, /* -40dBm */
	{ -45, 0x59be }, /* -45dBm */
	{ -50, 0x3a14 }, /* -50dBm */
	{ -55, 0x2d11 }, /* -55dBm */
	{ -60, 0x210d }, /* -60dBm */
	{ -65, 0xa14f }, /* -65dBm */
	{ -70, 0x07aa }	 /* -70dBm */
};


static struct stv090x_reg stv0900_initval[] = {

	{ STV090x_OUTCFG,		0x00 },
	{ STV090x_MODECFG,		0xff },
	{ STV090x_AGCRF1CFG,		0x11 },
	{ STV090x_AGCRF2CFG,		0x13 },
	{ STV090x_TSGENERAL1X,		0x14 },
	{ STV090x_TSTTNR2,		0x21 },
	{ STV090x_TSTTNR4,		0x21 },
	{ STV090x_P2_DISTXCTL,		0x22 },
	{ STV090x_P2_F22TX,		0xc0 },
	{ STV090x_P2_F22RX,		0xc0 },
	{ STV090x_P2_DISRXCTL,		0x00 },
	{ STV090x_P2_DMDCFGMD,		0xF9 },
	{ STV090x_P2_DEMOD,		0x08 },
	{ STV090x_P2_DMDCFG3,		0xc4 },
	{ STV090x_P2_CARFREQ,		0xed },
	{ STV090x_P2_LDT,		0xd0 },
	{ STV090x_P2_LDT2,		0xb8 },
	{ STV090x_P2_TMGCFG,		0xd2 },
	{ STV090x_P2_TMGTHRISE,		0x20 },
	{ STV090x_P1_TMGCFG,		0xd2 },

	{ STV090x_P2_TMGTHFALL,		0x00 },
	{ STV090x_P2_FECSPY,		0x88 },
	{ STV090x_P2_FSPYDATA,		0x3a },
	{ STV090x_P2_FBERCPT4,		0x00 },
	{ STV090x_P2_FSPYBER,		0x10 },
	{ STV090x_P2_ERRCTRL1,		0x35 },
	{ STV090x_P2_ERRCTRL2,		0xc1 },
	{ STV090x_P2_CFRICFG,		0xf8 },
	{ STV090x_P2_NOSCFG,		0x1c },
	{ STV090x_P2_DMDTOM,		0x20 },
	{ STV090x_P2_CORRELMANT,	0x70 },
	{ STV090x_P2_CORRELABS,		0x88 },
	{ STV090x_P2_AGC2O,		0x5b },
	{ STV090x_P2_AGC2REF,		0x38 },
	{ STV090x_P2_CARCFG,		0xe4 },
	{ STV090x_P2_ACLC,		0x1A },
	{ STV090x_P2_BCLC,		0x09 },
	{ STV090x_P2_CARHDR,		0x08 },
	{ STV090x_P2_KREFTMG,		0xc1 },
	{ STV090x_P2_SFRUPRATIO,	0xf0 },
	{ STV090x_P2_SFRLOWRATIO,	0x70 },
	{ STV090x_P2_SFRSTEP,		0x58 },
	{ STV090x_P2_TMGCFG2,		0x01 },
	{ STV090x_P2_CAR2CFG,		0x26 },
	{ STV090x_P2_BCLC2S2Q,		0x86 },
	{ STV090x_P2_BCLC2S28,		0x86 },
	{ STV090x_P2_SMAPCOEF7,		0x77 },
	{ STV090x_P2_SMAPCOEF6,		0x85 },
	{ STV090x_P2_SMAPCOEF5,		0x77 },
	{ STV090x_P2_TSCFGL,		0x20 },
	{ STV090x_P2_DMDCFG2,		0x3b },
	{ STV090x_P2_MODCODLST0,	0xff },
	{ STV090x_P2_MODCODLST1,	0xff },
	{ STV090x_P2_MODCODLST2,	0xff },
	{ STV090x_P2_MODCODLST3,	0xff },
	{ STV090x_P2_MODCODLST4,	0xff },
	{ STV090x_P2_MODCODLST5,	0xff },
	{ STV090x_P2_MODCODLST6,	0xff },
	{ STV090x_P2_MODCODLST7,	0xcc },
	{ STV090x_P2_MODCODLST8,	0xcc },
	{ STV090x_P2_MODCODLST9,	0xcc },
	{ STV090x_P2_MODCODLSTA,	0xcc },
	{ STV090x_P2_MODCODLSTB,	0xcc },
	{ STV090x_P2_MODCODLSTC,	0xcc },
	{ STV090x_P2_MODCODLSTD,	0xcc },
	{ STV090x_P2_MODCODLSTE,	0xcc },
	{ STV090x_P2_MODCODLSTF,	0xcf },
	{ STV090x_P1_DISTXCTL,		0x22 },
	{ STV090x_P1_F22TX,		0xc0 },
	{ STV090x_P1_F22RX,		0xc0 },
	{ STV090x_P1_DISRXCTL,		0x00 },
	{ STV090x_P1_DMDCFGMD,		0xf9 },
	{ STV090x_P1_DEMOD,		0x08 },
	{ STV090x_P1_DMDCFG3,		0xc4 },
	{ STV090x_P1_DMDTOM,		0x20 },
	{ STV090x_P1_CARFREQ,		0xed },
	{ STV090x_P1_LDT,		0xd0 },
	{ STV090x_P1_LDT2,		0xb8 },
	{ STV090x_P1_TMGCFG,		0xd2 },
	{ STV090x_P1_TMGTHRISE,		0x20 },
	{ STV090x_P1_TMGTHFALL,		0x00 },
	{ STV090x_P1_SFRUPRATIO,	0xf0 },
	{ STV090x_P1_SFRLOWRATIO,	0x70 },
	{ STV090x_P1_TSCFGL,		0x20 },
	{ STV090x_P1_FECSPY,		0x88 },
	{ STV090x_P1_FSPYDATA,		0x3a },
	{ STV090x_P1_FBERCPT4,		0x00 },
	{ STV090x_P1_FSPYBER,		0x10 },
	{ STV090x_P1_ERRCTRL1,		0x35 },
	{ STV090x_P1_ERRCTRL2,		0xc1 },
	{ STV090x_P1_CFRICFG,		0xf8 },
	{ STV090x_P1_NOSCFG,		0x1c },
	{ STV090x_P1_CORRELMANT,	0x70 },
	{ STV090x_P1_CORRELABS,		0x88 },
	{ STV090x_P1_AGC2O,		0x5b },
	{ STV090x_P1_AGC2REF,		0x38 },
	{ STV090x_P1_CARCFG,		0xe4 },
	{ STV090x_P1_ACLC,		0x1A },
	{ STV090x_P1_BCLC,		0x09 },
	{ STV090x_P1_CARHDR,		0x08 },
	{ STV090x_P1_KREFTMG,		0xc1 },
	{ STV090x_P1_SFRSTEP,		0x58 },
	{ STV090x_P1_TMGCFG2,		0x01 },
	{ STV090x_P1_CAR2CFG,		0x26 },
	{ STV090x_P1_BCLC2S2Q,		0x86 },
	{ STV090x_P1_BCLC2S28,		0x86 },
	{ STV090x_P1_SMAPCOEF7,		0x77 },
	{ STV090x_P1_SMAPCOEF6,		0x85 },
	{ STV090x_P1_SMAPCOEF5,		0x77 },
	{ STV090x_P1_DMDCFG2,		0x3b },
	{ STV090x_P1_MODCODLST0,	0xff },
	{ STV090x_P1_MODCODLST1,	0xff },
	{ STV090x_P1_MODCODLST2,	0xff },
	{ STV090x_P1_MODCODLST3,	0xff },
	{ STV090x_P1_MODCODLST4,	0xff },
	{ STV090x_P1_MODCODLST5,	0xff },
	{ STV090x_P1_MODCODLST6,	0xff },
	{ STV090x_P1_MODCODLST7,	0xcc },
	{ STV090x_P1_MODCODLST8,	0xcc },
	{ STV090x_P1_MODCODLST9,	0xcc },
	{ STV090x_P1_MODCODLSTA,	0xcc },
	{ STV090x_P1_MODCODLSTB,	0xcc },
	{ STV090x_P1_MODCODLSTC,	0xcc },
	{ STV090x_P1_MODCODLSTD,	0xcc },
	{ STV090x_P1_MODCODLSTE,	0xcc },
	{ STV090x_P1_MODCODLSTF,	0xcf },
	{ STV090x_GENCFG,		0x1d },
	{ STV090x_NBITER_NF4,		0x37 },
	{ STV090x_NBITER_NF5,		0x29 },
	{ STV090x_NBITER_NF6,		0x37 },
	{ STV090x_NBITER_NF7,		0x33 },
	{ STV090x_NBITER_NF8,		0x31 },
	{ STV090x_NBITER_NF9,		0x2f },
	{ STV090x_NBITER_NF10,		0x39 },
	{ STV090x_NBITER_NF11,		0x3a },
	{ STV090x_NBITER_NF12,		0x29 },
	{ STV090x_NBITER_NF13,		0x37 },
	{ STV090x_NBITER_NF14,		0x33 },
	{ STV090x_NBITER_NF15,		0x2f },
	{ STV090x_NBITER_NF16,		0x39 },
	{ STV090x_NBITER_NF17,		0x3a },
	{ STV090x_NBITERNOERR,		0x04 },
	{ STV090x_GAINLLR_NF4,		0x0C },
	{ STV090x_GAINLLR_NF5,		0x0F },
	{ STV090x_GAINLLR_NF6,		0x11 },
	{ STV090x_GAINLLR_NF7,		0x14 },
	{ STV090x_GAINLLR_NF8,		0x17 },
	{ STV090x_GAINLLR_NF9,		0x19 },
	{ STV090x_GAINLLR_NF10,		0x20 },
	{ STV090x_GAINLLR_NF11,		0x21 },
	{ STV090x_GAINLLR_NF12,		0x0D },
	{ STV090x_GAINLLR_NF13,		0x0F },
	{ STV090x_GAINLLR_NF14,		0x13 },
	{ STV090x_GAINLLR_NF15,		0x1A },
	{ STV090x_GAINLLR_NF16,		0x1F },
	{ STV090x_GAINLLR_NF17,		0x21 },
	{ STV090x_RCCFGH,		0x20 },
	{ STV090x_P1_FECM,		0x01 }, /* disable DSS modes */
	{ STV090x_P2_FECM,		0x01 }, /* disable DSS modes */
	{ STV090x_P1_PRVIT,		0x2F }, /* disable PR 6/7 */
	{ STV090x_P2_PRVIT,		0x2F }, /* disable PR 6/7 */
};

static struct stv090x_reg stv0903_initval[] = {
	{ STV090x_OUTCFG,		0x00 },
	{ STV090x_AGCRF1CFG,		0x11 },
	{ STV090x_STOPCLK1,		0x48 },
	{ STV090x_STOPCLK2,		0x14 },
	{ STV090x_TSTTNR1,		0x27 },
	{ STV090x_TSTTNR2,		0x21 },
	{ STV090x_P1_DISTXCTL,		0x22 },
	{ STV090x_P1_F22TX,		0xc0 },
	{ STV090x_P1_F22RX,		0xc0 },
	{ STV090x_P1_DISRXCTL,		0x00 },
	{ STV090x_P1_DMDCFGMD,		0xF9 },
	{ STV090x_P1_DEMOD,		0x08 },
	{ STV090x_P1_DMDCFG3,		0xc4 },
	{ STV090x_P1_CARFREQ,		0xed },
	{ STV090x_P1_TNRCFG2,		0x82 },
	{ STV090x_P1_LDT,		0xd0 },
	{ STV090x_P1_LDT2,		0xb8 },
	{ STV090x_P1_TMGCFG,		0xd2 },
	{ STV090x_P1_TMGTHRISE,		0x20 },
	{ STV090x_P1_TMGTHFALL,		0x00 },
	{ STV090x_P1_SFRUPRATIO,	0xf0 },
	{ STV090x_P1_SFRLOWRATIO,	0x70 },
	{ STV090x_P1_TSCFGL,		0x20 },
	{ STV090x_P1_FECSPY,		0x88 },
	{ STV090x_P1_FSPYDATA,		0x3a },
	{ STV090x_P1_FBERCPT4,		0x00 },
	{ STV090x_P1_FSPYBER,		0x10 },
	{ STV090x_P1_ERRCTRL1,		0x35 },
	{ STV090x_P1_ERRCTRL2,		0xc1 },
	{ STV090x_P1_CFRICFG,		0xf8 },
	{ STV090x_P1_NOSCFG,		0x1c },
	{ STV090x_P1_DMDTOM,		0x20 },
	{ STV090x_P1_CORRELMANT,	0x70 },
	{ STV090x_P1_CORRELABS,		0x88 },
	{ STV090x_P1_AGC2O,		0x5b },
	{ STV090x_P1_AGC2REF,		0x38 },
	{ STV090x_P1_CARCFG,		0xe4 },
	{ STV090x_P1_ACLC,		0x1A },
	{ STV090x_P1_BCLC,		0x09 },
	{ STV090x_P1_CARHDR,		0x08 },
	{ STV090x_P1_KREFTMG,		0xc1 },
	{ STV090x_P1_SFRSTEP,		0x58 },
	{ STV090x_P1_TMGCFG2,		0x01 },
	{ STV090x_P1_CAR2CFG,		0x26 },
	{ STV090x_P1_BCLC2S2Q,		0x86 },
	{ STV090x_P1_BCLC2S28,		0x86 },
	{ STV090x_P1_SMAPCOEF7,		0x77 },
	{ STV090x_P1_SMAPCOEF6,		0x85 },
	{ STV090x_P1_SMAPCOEF5,		0x77 },
	{ STV090x_P1_DMDCFG2,		0x3b },
	{ STV090x_P1_MODCODLST0,	0xff },
	{ STV090x_P1_MODCODLST1,	0xff },
	{ STV090x_P1_MODCODLST2,	0xff },
	{ STV090x_P1_MODCODLST3,	0xff },
	{ STV090x_P1_MODCODLST4,	0xff },
	{ STV090x_P1_MODCODLST5,	0xff },
	{ STV090x_P1_MODCODLST6,	0xff },
	{ STV090x_P1_MODCODLST7,	0xcc },
	{ STV090x_P1_MODCODLST8,	0xcc },
	{ STV090x_P1_MODCODLST9,	0xcc },
	{ STV090x_P1_MODCODLSTA,	0xcc },
	{ STV090x_P1_MODCODLSTB,	0xcc },
	{ STV090x_P1_MODCODLSTC,	0xcc },
	{ STV090x_P1_MODCODLSTD,	0xcc },
	{ STV090x_P1_MODCODLSTE,	0xcc },
	{ STV090x_P1_MODCODLSTF,	0xcf },
	{ STV090x_GENCFG,		0x1c },
	{ STV090x_NBITER_NF4,		0x37 },
	{ STV090x_NBITER_NF5,		0x29 },
	{ STV090x_NBITER_NF6,		0x37 },
	{ STV090x_NBITER_NF7,		0x33 },
	{ STV090x_NBITER_NF8,		0x31 },
	{ STV090x_NBITER_NF9,		0x2f },
	{ STV090x_NBITER_NF10,		0x39 },
	{ STV090x_NBITER_NF11,		0x3a },
	{ STV090x_NBITER_NF12,		0x29 },
	{ STV090x_NBITER_NF13,		0x37 },
	{ STV090x_NBITER_NF14,		0x33 },
	{ STV090x_NBITER_NF15,		0x2f },
	{ STV090x_NBITER_NF16,		0x39 },
	{ STV090x_NBITER_NF17,		0x3a },
	{ STV090x_NBITERNOERR,		0x04 },
	{ STV090x_GAINLLR_NF4,		0x0C },
	{ STV090x_GAINLLR_NF5,		0x0F },
	{ STV090x_GAINLLR_NF6,		0x11 },
	{ STV090x_GAINLLR_NF7,		0x14 },
	{ STV090x_GAINLLR_NF8,		0x17 },
	{ STV090x_GAINLLR_NF9,		0x19 },
	{ STV090x_GAINLLR_NF10,		0x20 },
	{ STV090x_GAINLLR_NF11,		0x21 },
	{ STV090x_GAINLLR_NF12,		0x0D },
	{ STV090x_GAINLLR_NF13,		0x0F },
	{ STV090x_GAINLLR_NF14,		0x13 },
	{ STV090x_GAINLLR_NF15,		0x1A },
	{ STV090x_GAINLLR_NF16,		0x1F },
	{ STV090x_GAINLLR_NF17,		0x21 },
	{ STV090x_RCCFGH,		0x20 },
	{ STV090x_P1_FECM,		0x01 }, /*disable the DSS mode */
	{ STV090x_P1_PRVIT,		0x2f }  /*disable puncture rate 6/7*/
};

static struct stv090x_reg stv0900_cut20_val[] = {

	{ STV090x_P2_DMDCFG3,		0xe8 },
	{ STV090x_P2_DMDCFG4,		0x10 },
	{ STV090x_P2_CARFREQ,		0x38 },
	{ STV090x_P2_CARHDR,		0x20 },
	{ STV090x_P2_KREFTMG,		0x5a },
	{ STV090x_P2_SMAPCOEF7,		0x06 },
	{ STV090x_P2_SMAPCOEF6,		0x00 },
	{ STV090x_P2_SMAPCOEF5,		0x04 },
	{ STV090x_P2_NOSCFG,		0x0c },
	{ STV090x_P1_DMDCFG3,		0xe8 },
	{ STV090x_P1_DMDCFG4,		0x10 },
	{ STV090x_P1_CARFREQ,		0x38 },
	{ STV090x_P1_CARHDR,		0x20 },
	{ STV090x_P1_KREFTMG,		0x5a },
	{ STV090x_P1_SMAPCOEF7,		0x06 },
	{ STV090x_P1_SMAPCOEF6,		0x00 },
	{ STV090x_P1_SMAPCOEF5,		0x04 },
	{ STV090x_P1_NOSCFG,		0x0c },
	{ STV090x_GAINLLR_NF4,		0x21 },
	{ STV090x_GAINLLR_NF5,		0x21 },
	{ STV090x_GAINLLR_NF6,		0x20 },
	{ STV090x_GAINLLR_NF7,		0x1F },
	{ STV090x_GAINLLR_NF8,		0x1E },
	{ STV090x_GAINLLR_NF9,		0x1E },
	{ STV090x_GAINLLR_NF10,		0x1D },
	{ STV090x_GAINLLR_NF11,		0x1B },
	{ STV090x_GAINLLR_NF12,		0x20 },
	{ STV090x_GAINLLR_NF13,		0x20 },
	{ STV090x_GAINLLR_NF14,		0x20 },
	{ STV090x_GAINLLR_NF15,		0x20 },
	{ STV090x_GAINLLR_NF16,		0x20 },
	{ STV090x_GAINLLR_NF17,		0x21 },
};

static struct stv090x_reg stv0903_cut20_val[] = {
	{ STV090x_P1_DMDCFG3,		0xe8 },
	{ STV090x_P1_DMDCFG4,		0x10 },
	{ STV090x_P1_CARFREQ,		0x38 },
	{ STV090x_P1_CARHDR,		0x20 },
	{ STV090x_P1_KREFTMG,		0x5a },
	{ STV090x_P1_SMAPCOEF7,		0x06 },
	{ STV090x_P1_SMAPCOEF6,		0x00 },
	{ STV090x_P1_SMAPCOEF5,		0x04 },
	{ STV090x_P1_NOSCFG,		0x0c },
	{ STV090x_GAINLLR_NF4,		0x21 },
	{ STV090x_GAINLLR_NF5,		0x21 },
	{ STV090x_GAINLLR_NF6,		0x20 },
	{ STV090x_GAINLLR_NF7,		0x1F },
	{ STV090x_GAINLLR_NF8,		0x1E },
	{ STV090x_GAINLLR_NF9,		0x1E },
	{ STV090x_GAINLLR_NF10,		0x1D },
	{ STV090x_GAINLLR_NF11,		0x1B },
	{ STV090x_GAINLLR_NF12,		0x20 },
	{ STV090x_GAINLLR_NF13,		0x20 },
	{ STV090x_GAINLLR_NF14,		0x20 },
	{ STV090x_GAINLLR_NF15,		0x20 },
	{ STV090x_GAINLLR_NF16,		0x20 },
	{ STV090x_GAINLLR_NF17,		0x21 }
};

/* Cut 1.x Long Frame Tracking CR loop */
static struct stv090x_long_frame_crloop stv090x_s2_crl[] = {
	/* MODCOD     2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_QPSK_12,  0x1c, 0x0d, 0x1b, 0x2c, 0x3a, 0x1c, 0x2a, 0x3b, 0x2a, 0x1b },
	{ STV090x_QPSK_35,  0x2c, 0x0d, 0x2b, 0x2c, 0x3a, 0x0c, 0x3a, 0x2b, 0x2a, 0x0b },
	{ STV090x_QPSK_23,  0x2c, 0x0d, 0x2b, 0x2c, 0x0b, 0x0c, 0x3a, 0x1b, 0x2a, 0x3a },
	{ STV090x_QPSK_34,  0x3c, 0x0d, 0x3b, 0x1c, 0x0b, 0x3b, 0x3a, 0x0b, 0x2a, 0x3a },
	{ STV090x_QPSK_45,  0x3c, 0x0d, 0x3b, 0x1c, 0x0b, 0x3b, 0x3a, 0x0b, 0x2a, 0x3a },
	{ STV090x_QPSK_56,  0x0d, 0x0d, 0x3b, 0x1c, 0x0b, 0x3b, 0x3a, 0x0b, 0x2a, 0x3a },
	{ STV090x_QPSK_89,  0x0d, 0x0d, 0x3b, 0x1c, 0x1b, 0x3b, 0x3a, 0x0b, 0x2a, 0x3a },
	{ STV090x_QPSK_910, 0x1d, 0x0d, 0x3b, 0x1c, 0x1b, 0x3b, 0x3a, 0x0b, 0x2a, 0x3a },
	{ STV090x_8PSK_35,  0x29, 0x3b, 0x09, 0x2b, 0x38, 0x0b, 0x18, 0x1a, 0x08, 0x0a },
	{ STV090x_8PSK_23,  0x0a, 0x3b, 0x29, 0x2b, 0x19, 0x0b, 0x38, 0x1a, 0x18, 0x0a },
	{ STV090x_8PSK_34,  0x3a, 0x3b, 0x2a, 0x2b, 0x39, 0x0b, 0x19, 0x1a, 0x38, 0x0a },
	{ STV090x_8PSK_56,  0x1b, 0x3b, 0x0b, 0x2b, 0x1a, 0x0b, 0x39, 0x1a, 0x19, 0x0a },
	{ STV090x_8PSK_89,  0x3b, 0x3b, 0x0b, 0x2b, 0x2a, 0x0b, 0x39, 0x1a, 0x29, 0x39 },
	{ STV090x_8PSK_910, 0x3b, 0x3b, 0x0b, 0x2b, 0x2a, 0x0b, 0x39, 0x1a, 0x29, 0x39 }
};

/* Cut 2.0 Long Frame Tracking CR loop */
static struct stv090x_long_frame_crloop stv090x_s2_crl_cut20[] = {
	/* MODCOD  2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_QPSK_12,  0x1f, 0x3f, 0x1e, 0x3f, 0x3d, 0x1f, 0x3d, 0x3e, 0x3d, 0x1e },
	{ STV090x_QPSK_35,  0x2f, 0x3f, 0x2e, 0x2f, 0x3d, 0x0f, 0x0e, 0x2e, 0x3d, 0x0e },
	{ STV090x_QPSK_23,  0x2f, 0x3f, 0x2e, 0x2f, 0x0e, 0x0f, 0x0e, 0x1e, 0x3d, 0x3d },
	{ STV090x_QPSK_34,  0x3f, 0x3f, 0x3e, 0x1f, 0x0e, 0x3e, 0x0e, 0x1e, 0x3d, 0x3d },
	{ STV090x_QPSK_45,  0x3f, 0x3f, 0x3e, 0x1f, 0x0e, 0x3e, 0x0e, 0x1e, 0x3d, 0x3d },
	{ STV090x_QPSK_56,  0x3f, 0x3f, 0x3e, 0x1f, 0x0e, 0x3e, 0x0e, 0x1e, 0x3d, 0x3d },
	{ STV090x_QPSK_89,  0x3f, 0x3f, 0x3e, 0x1f, 0x1e, 0x3e, 0x0e, 0x1e, 0x3d, 0x3d },
	{ STV090x_QPSK_910, 0x3f, 0x3f, 0x3e, 0x1f, 0x1e, 0x3e, 0x0e, 0x1e, 0x3d, 0x3d },
	{ STV090x_8PSK_35,  0x3c, 0x3e, 0x1c, 0x2e, 0x0c, 0x1e, 0x2b, 0x2d, 0x1b, 0x1d },
	{ STV090x_8PSK_23,  0x1d, 0x3e, 0x3c, 0x2e, 0x2c, 0x1e, 0x0c, 0x2d, 0x2b, 0x1d },
	{ STV090x_8PSK_34,  0x0e, 0x3e, 0x3d, 0x2e, 0x0d, 0x1e, 0x2c, 0x2d, 0x0c, 0x1d },
	{ STV090x_8PSK_56,  0x2e, 0x3e, 0x1e, 0x2e, 0x2d, 0x1e, 0x3c, 0x2d, 0x2c, 0x1d },
	{ STV090x_8PSK_89,  0x3e, 0x3e, 0x1e, 0x2e, 0x3d, 0x1e, 0x0d, 0x2d, 0x3c, 0x1d },
	{ STV090x_8PSK_910, 0x3e, 0x3e, 0x1e, 0x2e, 0x3d, 0x1e, 0x1d, 0x2d, 0x0d, 0x1d }
};


/* Cut 2.0 Long Frame Tracking CR Loop */
static struct stv090x_long_frame_crloop stv090x_s2_apsk_crl_cut20[] = {
	/* MODCOD  2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_16APSK_23,  0x0c, 0x0c, 0x0c, 0x0c, 0x1d, 0x0c, 0x3c, 0x0c, 0x2c, 0x0c },
	{ STV090x_16APSK_34,  0x0c, 0x0c, 0x0c, 0x0c, 0x0e, 0x0c, 0x2d, 0x0c, 0x1d, 0x0c },
	{ STV090x_16APSK_45,  0x0c, 0x0c, 0x0c, 0x0c, 0x1e, 0x0c, 0x3d, 0x0c, 0x2d, 0x0c },
	{ STV090x_16APSK_56,  0x0c, 0x0c, 0x0c, 0x0c, 0x1e, 0x0c, 0x3d, 0x0c, 0x2d, 0x0c },
	{ STV090x_16APSK_89,  0x0c, 0x0c, 0x0c, 0x0c, 0x2e, 0x0c, 0x0e, 0x0c, 0x3d, 0x0c },
	{ STV090x_16APSK_910, 0x0c, 0x0c, 0x0c, 0x0c, 0x2e, 0x0c, 0x0e, 0x0c, 0x3d, 0x0c },
	{ STV090x_32APSK_34,  0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c },
	{ STV090x_32APSK_45,  0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c },
	{ STV090x_32APSK_56,  0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c },
	{ STV090x_32APSK_89,  0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c },
	{ STV090x_32APSK_910, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c }
};


static struct stv090x_long_frame_crloop stv090x_s2_lowqpsk_crl_cut20[] = {
	/* MODCOD  2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_QPSK_14,  0x0f, 0x3f, 0x0e, 0x3f, 0x2d, 0x2f, 0x2d, 0x1f, 0x3d, 0x3e },
	{ STV090x_QPSK_13,  0x0f, 0x3f, 0x0e, 0x3f, 0x2d, 0x2f, 0x3d, 0x0f, 0x3d, 0x2e },
	{ STV090x_QPSK_25,  0x1f, 0x3f, 0x1e, 0x3f, 0x3d, 0x1f, 0x3d, 0x3e, 0x3d, 0x2e }
};


/* Cut 1.2 & 2.0 Short Frame Tracking CR Loop */
static struct stv090x_short_frame_crloop stv090x_s2_short_crl[] = {
	/* MODCOD  2M_cut1.2 2M_cut2.0 5M_cut1.2 5M_cut2.0 10M_cut1.2 10M_cut2.0 20M_cut1.2 20M_cut2.0 30M_cut1.2 30M_cut2.0 */
	{ STV090x_QPSK,   0x3c, 0x2f, 0x2b, 0x2e, 0x0b, 0x0e, 0x3a, 0x0e, 0x2a, 0x3d },
	{ STV090x_8PSK,   0x0b, 0x3e, 0x2a, 0x0e, 0x0a, 0x2d, 0x19, 0x0d, 0x09, 0x3c },
	{ STV090x_16APSK, 0x1b, 0x1e, 0x1b, 0x1e, 0x1b, 0x1e, 0x3a, 0x3d, 0x2a, 0x2d },
	{ STV090x_32APSK, 0x1b, 0x1e, 0x1b, 0x1e, 0x1b, 0x1e, 0x3a, 0x3d, 0x2a, 0x2d }
};


static inline s32 comp2(s32 __x, s32 __width)
{
	if (__width == 32)
		return __x;
	else
		return (__x >= (1 << (__width - 1))) ? (__x - (1 << __width)) : __x;
}

static int stv090x_read_reg(struct stv090x_state *state, unsigned int reg)
{
	const struct stv090x_config *config = state->config;
	int ret;

	u8 b0[] = { reg >> 8, reg & 0xff };
	u8 buf;

	struct i2c_msg msg[] = {
		{ .addr	= config->address, .flags	= 0, 		.buf = b0,   .len = 2 },
		{ .addr	= config->address, .flags	= I2C_M_RD,	.buf = &buf, .len = 1 }
	};

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2) {
		if (ret != -ERESTARTSYS)
			dprintk(FE_ERROR, 1,
				"Read error, Reg=[0x%02x], Status=%d",
				reg, ret);

		return ret < 0 ? ret : -EREMOTEIO;
	}
	if (unlikely(*state->verbose >= FE_DEBUGREG))
		dprintk(FE_ERROR, 1, "Reg=[0x%02x], data=%02x",
			reg, buf);

	return (unsigned int) buf;
}

static int stv090x_write_regs(struct stv090x_state *state, unsigned int reg, u8 *data, u32 count)
{
	const struct stv090x_config *config = state->config;
	int ret;
	u8 buf[2 + count];
	struct i2c_msg i2c_msg = { .addr = config->address, .flags = 0, .buf = buf, .len = 2 + count };

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	memcpy(&buf[2], data, count);

	if (unlikely(*state->verbose >= FE_DEBUGREG)) {
		int i;

		printk(KERN_DEBUG "%s [0x%04x]:", __func__, reg);
		for (i = 0; i < count; i++)
			printk(" %02x", data[i]);
		printk("\n");
	}

	ret = i2c_transfer(state->i2c, &i2c_msg, 1);
	if (ret != 1) {
		if (ret != -ERESTARTSYS)
			dprintk(FE_ERROR, 1, "Reg=[0x%04x], Data=[0x%02x ...], Count=%u, Status=%d",
				reg, data[0], count, ret);
		return ret < 0 ? ret : -EREMOTEIO;
	}

	return 0;
}

static int stv090x_write_reg(struct stv090x_state *state, unsigned int reg, u8 data)
{
	return stv090x_write_regs(state, reg, &data, 1);
}

static int stv090x_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	reg = STV090x_READ_DEMOD(state, I2CRPT);
	if (enable) {
		dprintk(FE_DEBUG, 1, "Enable Gate");
		STV090x_SETFIELD_Px(reg, I2CT_ON_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, I2CRPT, reg) < 0)
			goto err;

	} else {
		dprintk(FE_DEBUG, 1, "Disable Gate");
		STV090x_SETFIELD_Px(reg, I2CT_ON_FIELD, 0);
		if ((STV090x_WRITE_DEMOD(state, I2CRPT, reg)) < 0)
			goto err;
	}
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static void stv090x_get_lock_tmg(struct stv090x_state *state)
{
	switch (state->algo) {
	case STV090x_BLIND_SEARCH:
		dprintk(FE_DEBUG, 1, "Blind Search");
		if (state->srate <= 1500000) {  /*10Msps< SR <=15Msps*/
			state->DemodTimeout = 1500;
			state->FecTimeout = 400;
		} else if (state->srate <= 5000000) {  /*10Msps< SR <=15Msps*/
			state->DemodTimeout = 1000;
			state->FecTimeout = 300;
		} else {  /*SR >20Msps*/
			state->DemodTimeout = 700;
			state->FecTimeout = 100;
		}
		break;

	case STV090x_COLD_SEARCH:
	case STV090x_WARM_SEARCH:
	default:
		dprintk(FE_DEBUG, 1, "Normal Search");
		if (state->srate <= 1000000) {  /*SR <=1Msps*/
			state->DemodTimeout = 4500;
			state->FecTimeout = 1700;
		} else if (state->srate <= 2000000) { /*1Msps < SR <= 2Msps */
			state->DemodTimeout = 2500;
			state->FecTimeout = 1100;
		} else if (state->srate <= 5000000) { /*2Msps < SR <= 5Msps */
			state->DemodTimeout = 1000;
			state->FecTimeout = 550;
		} else if (state->srate <= 10000000) { /*5Msps < SR <= 10Msps */
			state->DemodTimeout = 700;
			state->FecTimeout = 250;
		} else if (state->srate <= 20000000) { /*10Msps < SR <= 20Msps */
			state->DemodTimeout = 400;
			state->FecTimeout = 130;
		} else {   /*SR >20Msps*/
			state->DemodTimeout = 300;
			state->FecTimeout = 100;
		}
		break;
	}

	if (state->algo == STV090x_WARM_SEARCH)
		state->DemodTimeout /= 2;
}

static int stv090x_set_srate(struct stv090x_state *state, u32 srate)
{
	u32 sym;

	if (srate > 6000000) {
		sym  = (srate / 1000) * 65536;
		sym /= (state->mclk / 1000);
	} else {
		sym  = (srate / 100) * 65536;
		sym /= (state->mclk / 100);
	}

	if (STV090x_WRITE_DEMOD(state, SFRINIT1, (sym >> 8) & 0xff) < 0) /* MSB */
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRINIT0, (sym & 0xff)) < 0) /* LSB */
		goto err;
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_set_max_srate(struct stv090x_state *state, u32 clk, u32 srate)
{
	u32 sym;

	srate = 105 * (srate / 100);
	if (srate > 6000000) {
		sym  = (srate / 1000) * 65536;
		sym /= (clk / 1000);
	} else {
		sym  = (srate / 100) * 65536;
		sym /= (clk / 100);
	}
	if (STV090x_WRITE_DEMOD(state, SFRUP1, (sym >> 8) & 0x7f) < 0) /* MSB */
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRUP0, sym & 0xff) < 0) /* LSB */
		goto err;
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_set_min_srate(struct stv090x_state *state, u32 clk, u32 srate)
{
	u32 sym;

	srate = 95 * (srate / 100);
	if (srate > 6000000) {
		sym  = (srate / 1000) * 65536;
		sym /= (clk / 1000);
	} else {
		sym  = (srate / 100) * 65536;
		sym /= (clk / 100);
	}
	if (STV090x_WRITE_DEMOD(state, SFRLOW1, ((sym >> 8) & 0xff)) < 0) /* MSB */
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRLOW0, (sym & 0xff)) < 0) /* LSB */
		goto err;
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static u32 stv090x_car_width(u32 srate, u32 rolloff)
{
	return srate + (srate * rolloff) / 100;
}

static int stv090x_set_vit_thacq(struct stv090x_state *state)
{
	if (STV090x_WRITE_DEMOD(state, VTH12, 0x96) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH23, 0x64) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH34, 0x36) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH56, 0x23) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH67, 0x1e) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH78, 0x19) < 0)
		goto err;
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_set_vit_thtracq(struct stv090x_state *state)
{
	if (STV090x_WRITE_DEMOD(state, VTH12, 0xd0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH23, 0x7d) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH34, 0x53) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH56, 0x2f) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH67, 0x24) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, VTH78, 0x1f) < 0)
		goto err;
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_set_viterbi(struct stv090x_state *state)
{
	switch (state->search_mode) {
	case STV090x_SEARCH_AUTO:
		if (STV090x_WRITE_DEMOD(state, FECM, 0x10) < 0) /* DVB-S and DVB-S2 */
			goto err;
		if (STV090x_WRITE_DEMOD(state, PRVIT, 0x3f) < 0) /* all puncture rate */
			goto err;
		break;
	case STV090x_SEARCH_DVBS1:
		if (STV090x_WRITE_DEMOD(state, FECM, 0x00) < 0) /* disable DSS */
			goto err;
		switch (state->fec) {
		case STV090x_PR12:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x01) < 0)
				goto err;
			break;

		case STV090x_PR23:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x02) < 0)
				goto err;
			break;

		case STV090x_PR34:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x04) < 0)
				goto err;
			break;

		case STV090x_PR56:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x08) < 0)
				goto err;
			break;

		case STV090x_PR78:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x20) < 0)
				goto err;
			break;

		default:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x2f) < 0) /* all */
				goto err;
			break;
		}
		break;
	case STV090x_SEARCH_DSS:
		if (STV090x_WRITE_DEMOD(state, FECM, 0x80) < 0)
			goto err;
		switch (state->fec) {
		case STV090x_PR12:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x01) < 0)
				goto err;
			break;

		case STV090x_PR23:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x02) < 0)
				goto err;
			break;

		case STV090x_PR67:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x10) < 0)
				goto err;
			break;

		default:
			if (STV090x_WRITE_DEMOD(state, PRVIT, 0x13) < 0) /* 1/2, 2/3, 6/7 */
				goto err;
			break;
		}
		break;
	default:
		break;
	}
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_stop_modcod(struct stv090x_state *state)
{
	if (STV090x_WRITE_DEMOD(state, MODCODLST0, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST1, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST2, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST3, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST4, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST5, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST6, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST7, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST8, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLST9, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLSTA, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLSTB, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLSTC, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLSTD, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLSTE, 0xff) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, MODCODLSTF, 0xff) < 0)
		goto err;
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_activate_modcod(struct stv090x_state *state)
{
	u32 matype, modcod, f_mod, index;

	if (state->dev_ver <= 0x11) {
		msleep(5);
		modcod = STV090x_READ_DEMOD(state, PLHMODCOD);
		matype = modcod & 0x03;
		modcod = (modcod & 0x7f) >> 2;
		index = STV090x_ADDR_OFFST(state, MODCODLSTF) - (modcod / 2);

		switch (matype) {
		default:
		case 0:
			f_mod = 14;
			break;
		case 1:
			f_mod = 13;
			break;
		case 2:
			f_mod = 11;
			break;
		case 3:
			f_mod = 7;
			break;
		}
		if (matype <= 1) {
			if (modcod % 2) {
				if (stv090x_write_reg(state, index, 0xf0 | f_mod) < 0)
					goto err;
			} else {
				if (stv090x_write_reg(state, index, (f_mod << 4) | 0x0f) < 0)
					goto err;
			}
		}
	} else if (state->dev_ver >= 0x12) {
			if (STV090x_WRITE_DEMOD(state, MODCODLST0, 0xff) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST1, 0xfc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST2, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST3, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST4, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST5, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST6, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST7, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST8, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLST9, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLSTA, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLSTB, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLSTC, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLSTD, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLSTE, 0xcc) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, MODCODLSTF, 0xcf) < 0)
				goto err;
	}
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_vitclk_ctl(struct stv090x_state *state, int enable)
{
	u32 reg;

	switch (state->demod) {
	case STV090x_DEMODULATOR_0:
		mutex_lock(&demod_lock);
		reg = stv090x_read_reg(state, STV090x_STOPCLK2);
		STV090x_SETFIELD(reg, STOP_CLKVIT1_FIELD, enable);
		if (stv090x_write_reg(state, STV090x_STOPCLK2, reg) < 0)
			goto err;
		mutex_unlock(&demod_lock);
		break;

	case STV090x_DEMODULATOR_1:
		mutex_lock(&demod_lock);
		reg = stv090x_read_reg(state, STV090x_STOPCLK2);
		STV090x_SETFIELD(reg, STOP_CLKVIT2_FIELD, enable);
		if (stv090x_write_reg(state, STV090x_STOPCLK2, reg) < 0)
			goto err;
		mutex_unlock(&demod_lock);
		break;

	default:
		dprintk(FE_ERROR, 1, "Wrong demodulator!");
		break;
	}
	return 0;
err:
	mutex_unlock(&demod_lock);
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_delivery_search(struct stv090x_state *state)
{
	u32 reg;

	switch (state->search_mode) {
	case STV090x_SEARCH_DVBS1:
	case STV090x_SEARCH_DSS:
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
		STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;

		/* Activate Viterbi decoder in legacy search, do not use FRESVIT1, might impact VITERBI2 */
		if (stv090x_vitclk_ctl(state, 0) < 0)
			goto err;

		if (STV090x_WRITE_DEMOD(state, ACLC, 0x1a) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, BCLC, 0x09) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x22) < 0) /* disable DVB-S2 */
			goto err;

		stv090x_set_vit_thacq(state);
		stv090x_set_viterbi(state);
		break;

	case STV090x_SEARCH_DVBS2:
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 0);
		STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;
		STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
		STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;

		if (stv090x_vitclk_ctl(state, 1) < 0)
			goto err;

		if (STV090x_WRITE_DEMOD(state, ACLC, 0x1a) < 0) /* stop DVB-S CR loop */
			goto err;
		if (STV090x_WRITE_DEMOD(state, BCLC, 0x09) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x26) < 0)
			goto err;

		if (state->demod_mode != STV090x_SINGLE) {
			if (state->dev_ver <= 0x11) /* 900 in dual TS mode */
				stv090x_stop_modcod(state);
			else
				stv090x_activate_modcod(state);
		}
		break;

	case STV090x_SEARCH_AUTO:
	default:
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 0);
		STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;
		STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
		STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;

		if (stv090x_vitclk_ctl(state, 1) < 0)
			goto err;

		if (STV090x_WRITE_DEMOD(state, ACLC, 0x1a) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, ACLC, 0x09) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x26) < 0)
			goto err;

		if (state->demod_mode != STV090x_SINGLE) {
			if (state->dev_ver <= 0x11) /* 900 in dual TS mode */
				stv090x_stop_modcod(state);
			else
				stv090x_activate_modcod(state);
		}
		stv090x_set_vit_thacq(state);
		stv090x_set_viterbi(state);
		break;
	}
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_start_search(struct stv090x_state *state)
{
	u32 reg;

	reg = STV090x_READ_DEMOD(state, DMDISTATE);
	STV090x_SETFIELD_Px(reg, I2C_DEMOD_MODE_FIELD, 0x1f);
	if (STV090x_WRITE_DEMOD(state, DMDISTATE, reg) < 0)
		goto err;

	if (state->dev_ver == 0x10) {
		if (STV090x_WRITE_DEMOD(state, CORRELEXP, 0xaa) < 0)
			goto err;
	}
	if (state->dev_ver < 0x20) {
		if (STV090x_WRITE_DEMOD(state, CARHDR, 0x55) < 0)
			goto err;
	}
	if (state->srate <= 5000000) {
		if (STV090x_WRITE_DEMOD(state, CARCFG, 0x44) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRUP1, 0x0f) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRUP1, 0xff) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRLOW1, 0xf0) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRLOW0, 0x00) < 0)
			goto err;

		/*enlarge the timing bandwith for Low SR*/
		if (STV090x_WRITE_DEMOD(state, RTCS2, 0x68) < 0)
			goto err;
	} else {
		/* If the symbol rate is >5 Msps
		Set The carrier search up and low to auto mode */
		if (STV090x_WRITE_DEMOD(state, CARCFG, 0xc4) < 0)
			goto err;
		/*reduce the timing bandwith for high SR*/
		if (STV090x_WRITE_DEMOD(state, RTCS2, 0x44) < 0)
			goto err;
	}
	if (STV090x_WRITE_DEMOD(state, CFRINIT1, 0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, CFRINIT0, 0) < 0)
		goto err;

	if (state->dev_ver >= 0x20) {
		if (STV090x_WRITE_DEMOD(state, EQUALCFG, 0x41) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, FFECFG, 0x41) < 0)
			goto err;

		if ((state->search_mode == STV090x_DVBS1)	||
			(state->search_mode == STV090x_DSS)	||
			(state->search_mode == STV090x_SEARCH_AUTO)) {

			if (STV090x_WRITE_DEMOD(state, VITSCALE, 0x82) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, VAVSRVIT, 0x00) < 0)
				goto err;
		}
	}

	if (STV090x_WRITE_DEMOD(state, SFRSTEP, 0x00) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0xe0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0xc0) < 0)
		goto err;

	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 0);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0);
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
		goto err;
	reg = STV090x_READ_DEMOD(state, DMDCFG2);
	STV090x_SETFIELD_Px(reg, S1S2_SEQUENTIAL_FIELD, 0x0);
	if (STV090x_WRITE_DEMOD(state, DMDCFG2, reg) < 0)
		goto err;

	if (state->dev_ver >= 0x20) { /*Frequency offset detector setting*/
		if (state->srate < 10000000) {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x4c) < 0)
				goto err;
		} else {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x4b) < 0)
				goto err;
		}
	} else {
		if (state->srate < 10000000) {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xef) < 0)
				goto err;
		} else {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xed) < 0)
				goto err;
		}
	}

	switch (state->algo) {
	case STV090x_WARM_SEARCH:/*The symbol rate and the exact carrier Frequency are known */
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
			goto err;
		break;

	case STV090x_COLD_SEARCH:/*The symbol rate is known*/
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0)
			goto err;
		break;

	default:
		break;
	}
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_get_agc2_min_level(struct stv090x_state *state)
{
	u32 agc2_min = 0, agc2 = 0, freq_init, freq_step, reg;
	s32 i, j, steps, dir;

	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
		goto err;
	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 1);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 1);
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
		goto err;

	if (STV090x_WRITE_DEMOD(state, SFRUP1, 0x83) < 0) /* SR = 65 Msps Max */
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRUP0, 0xc0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRLOW1, 0x82) < 0) /* SR= 400 ksps Min */
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRLOW0, 0xa0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x00) < 0) /* stop acq @ coarse carrier state */
		goto err;
	stv090x_set_srate(state, 1000000);

	steps  = -1 + state->search_range / 1000000;
	steps /= 2;
	steps  = (2 * steps) + 1;
	if (steps < 0)
		steps = 1;

	dir = 1;
	freq_step = (1000000 * 256) / (state->mclk / 256);
	freq_init = 0;

	for (i = 0; i < steps; i++) {
		if (dir > 0)
			freq_init = freq_init + (freq_step * i);
		else
			freq_init = freq_init - (freq_step * i);

		dir = -1;

		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x5c) < 0) /* Demod RESET */
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, (freq_init >> 8) & 0xff) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, freq_init & 0xff) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x58) < 0) /* Demod RESET */
			goto err;
		msleep(10);
		for (j = 0; j < 10; j++) {
			agc2 += STV090x_READ_DEMOD(state, AGC2I1) << 8;
			agc2 |= STV090x_READ_DEMOD(state, AGC2I0);
		}
		agc2 /= 10;
		agc2_min = 0xffff;
		if (agc2 < 0xffff)
			agc2_min = agc2;
	}

	return agc2_min;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static u32 stv090x_get_srate(struct stv090x_state *state, u32 clk)
{
	u8 r3, r2, r1, r0;
	s32 srate, int_1, int_2, tmp_1, tmp_2;
	u32 pow2;

	r3 = STV090x_READ_DEMOD(state, SFR3);
	r2 = STV090x_READ_DEMOD(state, SFR2);
	r1 = STV090x_READ_DEMOD(state, SFR1);
	r0 = STV090x_READ_DEMOD(state, SFR0);

	srate = ((r3 << 24) | (r2 << 16) | (r1 <<  8) | r0);

	pow2 = 1 << 16;
	int_1 = clk / pow2;
	int_2 = srate / pow2;

	tmp_1 = clk % pow2;
	tmp_2 = srate % pow2;

	srate = (int_1 * int_2) +
		((int_1 * tmp_2) / pow2) +
		((int_2 * tmp_1) / pow2);

	return srate;
}

static u32 stv090x_srate_srch_coarse(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;

	int tmg_lock = 0, i;
	s32 tmg_cpt = 0, dir = 1, steps, cur_step = 0, freq;
	u32 srate_coarse = 0, agc2 = 0, car_step = 1200, reg;

	reg = STV090x_READ_DEMOD(state, DMDISTATE);
	STV090x_SETFIELD_Px(reg, I2C_DEMOD_MODE_FIELD, 0x1f); /* Demod RESET */
	if (STV090x_WRITE_DEMOD(state, DMDISTATE, reg) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGCFG, 0x12) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0xf0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0xe0) < 0)
		goto err;
	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 1);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 1);
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
		goto err;

	if (STV090x_WRITE_DEMOD(state, SFRUP1, 0x83) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRUP0, 0xc0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRLOW1, 0x82) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, SFRLOW0, 0xa0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x00) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x60) < 0)
		goto err;

	if (state->dev_ver >= 0x20) {
		if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x6a) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, SFRSTEP, 0x95) < 0)
			goto err;
	} else {
		if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xed) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, SFRSTEP, 0x73) < 0)
			goto err;
	}

	if (state->srate <= 2000000)
		car_step = 1000;
	else if (state->srate <= 5000000)
		car_step = 2000;
	else if (state->srate <= 12000000)
		car_step = 3000;
	else
		car_step = 5000;

	steps  = -1 + ((state->search_range / 1000) / car_step);
	steps /= 2;
	steps  = (2 * steps) + 1;
	if (steps < 0)
		steps = 1;
	else if (steps > 10) {
		steps = 11;
		car_step = (state->search_range / 1000) / 10;
	}
	cur_step = 0;
	dir = 1;
	freq = state->frequency;

	while ((!tmg_lock) && (cur_step < steps)) {
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x5f) < 0) /* Demod RESET */
			goto err;
		reg = STV090x_READ_DEMOD(state, DMDISTATE);
		STV090x_SETFIELD_Px(reg, I2C_DEMOD_MODE_FIELD, 0x00); /* trigger acquisition */
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, reg) < 0)
			goto err;
		msleep(50);
		for (i = 0; i < 10; i++) {
			reg = STV090x_READ_DEMOD(state, DSTATUS);
			if (STV090x_GETFIELD_Px(reg, TMGLOCK_QUALITY_FIELD) >= 2)
				tmg_cpt++;
			agc2 += STV090x_READ_DEMOD(state, AGC2I1) << 8;
			agc2 |= STV090x_READ_DEMOD(state, AGC2I0);
		}
		agc2 /= 10;
		srate_coarse = stv090x_get_srate(state, state->mclk);
		cur_step++;
		dir *= -1;
		if ((tmg_cpt >= 5) && (agc2 < 0x1f00) && (srate_coarse < 55000000) && (srate_coarse > 850000))
			tmg_lock = 1;
		else if (cur_step < steps) {
			if (dir > 0)
				freq += cur_step * car_step;
			else
				freq -= cur_step * car_step;

			/* Setup tuner */
			stv090x_i2c_gate_ctrl(fe, 1);

			if (state->config->tuner_set_frequency)
				state->config->tuner_set_frequency(fe, state->frequency);

			if (state->config->tuner_set_bandwidth)
				state->config->tuner_set_bandwidth(fe, state->tuner_bw);

			stv090x_i2c_gate_ctrl(fe, 0);
			msleep(50);
			stv090x_i2c_gate_ctrl(fe, 1);

			if (state->config->tuner_get_status)
				state->config->tuner_get_status(fe, &reg);

			if (reg)
				dprintk(FE_DEBUG, 1, "Tuner phase locked");
			else
				dprintk(FE_DEBUG, 1, "Tuner unlocked");

			stv090x_i2c_gate_ctrl(fe, 0);

		}
	}
	if (!tmg_lock)
		srate_coarse = 0;
	else
		srate_coarse = stv090x_get_srate(state, state->mclk);

	return srate_coarse;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static u32 stv090x_srate_srch_fine(struct stv090x_state *state)
{
	u32 srate_coarse, freq_coarse, sym, reg;

	srate_coarse = stv090x_get_srate(state, state->mclk);
	freq_coarse  = STV090x_READ_DEMOD(state, CFR2) << 8;
	freq_coarse |= STV090x_READ_DEMOD(state, CFR1);
	sym = 13 * (srate_coarse / 10); /* SFRUP = SFR + 30% */

	if (sym < state->srate)
		srate_coarse = 0;
	else {
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0) /* Demod RESET */
			goto err;
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0x01) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0x20) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0x00) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, TMGCFG, 0xd2) < 0)
			goto err;
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;

		if (state->dev_ver >= 0x20) {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x49) < 0)
				goto err;
		} else {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xed) < 0)
				goto err;
		}

		if (srate_coarse > 3000000) {
			sym  = 13 * (srate_coarse / 10); /* SFRUP = SFR + 30% */
			sym  = (sym / 1000) * 65536;
			sym /= (state->mclk / 1000);
			if (STV090x_WRITE_DEMOD(state, SFRUP1, (sym >> 8) & 0x7f) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, SFRUP0, sym & 0xff) < 0)
				goto err;
			sym  = 10 * (srate_coarse / 13); /* SFRLOW = SFR - 30% */
			sym  = (sym / 1000) * 65536;
			sym /= (state->mclk / 1000);
			if (STV090x_WRITE_DEMOD(state, SFRLOW1, (sym >> 8) & 0x7f) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, SFRLOW0, sym & 0xff) < 0)
				goto err;
			sym  = (srate_coarse / 1000) * 65536;
			sym /= (state->mclk / 1000);
			if (STV090x_WRITE_DEMOD(state, SFRINIT1, (sym >> 8) & 0xff) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, SFRINIT0, sym & 0xff) < 0)
				goto err;
		} else {
			sym  = 13 * (srate_coarse / 10); /* SFRUP = SFR + 30% */
			sym  = (sym / 100) * 65536;
			sym /= (state->mclk / 100);
			if (STV090x_WRITE_DEMOD(state, SFRUP1, (sym >> 8) & 0x7f) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, SFRUP0, sym & 0xff) < 0)
				goto err;
			sym  = 10 * (srate_coarse / 14); /* SFRLOW = SFR - 30% */
			sym  = (sym / 100) * 65536;
			sym /= (state->mclk / 100);
			if (STV090x_WRITE_DEMOD(state, SFRLOW1, (sym >> 8) & 0x7f) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, SFRLOW0, sym & 0xff) < 0)
				goto err;
			sym  = (srate_coarse / 100) * 65536;
			sym /= (state->mclk / 100);
			if (STV090x_WRITE_DEMOD(state, SFRINIT1, (sym >> 8) & 0xff) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, SFRINIT0, sym & 0xff) < 0)
				goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x20) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, (freq_coarse >> 8) & 0xff) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, freq_coarse & 0xff) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0) /* trigger acquisition */
			goto err;
	}

	return srate_coarse;

err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_get_dmdlock(struct stv090x_state *state, s32 timeout)
{
	s32 timer = 0, lock = 0;
	u32 reg;
	u8 stat;

	while ((timer < timeout) && (!lock)) {
		reg = STV090x_READ_DEMOD(state, DMDSTATE);
		stat = STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD);

		switch (stat) {
		case 0: /* searching */
		case 1: /* first PLH detected */
		default:
			dprintk(FE_DEBUG, 1, "Demodulator searching ..");
			lock = 0;
			break;
		case 2: /* DVB-S2 mode */
		case 3: /* DVB-S1/legacy mode */
			reg = STV090x_READ_DEMOD(state, DSTATUS);
			lock = STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD);
			break;
		}

		if (!lock)
			msleep(10);
		else
			dprintk(FE_DEBUG, 1, "Demodulator acquired LOCK");

		timer += 10;
	}
	return lock;
}

static int stv090x_blind_search(struct stv090x_state *state)
{
	u32 agc2, reg, srate_coarse;
	s32 timeout_dmd = 500, cpt_fail, agc2_ovflw, i;
	u8 k_ref, k_max, k_min;
	int coarse_fail, lock;

	if (state->dev_ver < 0x20) {
		k_max = 233;
		k_min = 143;
	} else {
		k_max = 120;
		k_min = 30;
	}

	agc2 = stv090x_get_agc2_min_level(state);

	if (agc2 > STV090x_SEARCH_AGC2_TH) {
		lock = 0;
	} else {
		if (state->dev_ver == 0x10) {
			if (STV090x_WRITE_DEMOD(state, CORRELEXP, 0xaa) < 0)
				goto err;
		}
		if (state->dev_ver < 0x20) {
			if (STV090x_WRITE_DEMOD(state, CARHDR, 0x55) < 0)
				goto err;
		}

		if (STV090x_WRITE_DEMOD(state, CARCFG, 0xc4) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, RTCS2, 0x44) < 0)
			goto err;
		if (state->dev_ver >= 0x20) {
			if (STV090x_WRITE_DEMOD(state, EQUALCFG, 0x41) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, FFECFG, 0x41) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, VITSCALE, 0x82) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, VAVSRVIT, 0x00) < 0) /* set viterbi hysteresis */
				goto err;
		}

		k_ref = k_max;
		do {
			if (STV090x_WRITE_DEMOD(state, KREFTMG, k_ref) < 0)
				goto err;
			if (stv090x_srate_srch_coarse(state) != 0) {
				srate_coarse = stv090x_srate_srch_fine(state);
				if (srate_coarse != 0) {
					stv090x_get_lock_tmg(state);
					lock = stv090x_get_dmdlock(state, timeout_dmd);
				} else {
					lock = 0;
				}
			} else {
				cpt_fail = 0;
				agc2_ovflw = 0;
				for (i = 0; i < 10; i++) {
					agc2  = STV090x_READ_DEMOD(state, AGC2I1) << 8;
					agc2 |= STV090x_READ_DEMOD(state, AGC2I0);
					if (agc2 >= 0xff00)
						agc2_ovflw++;
					reg = STV090x_READ_DEMOD(state, DSTATUS2);
					if ((STV090x_GETFIELD_Px(reg, CFR_OVERFLOW_FIELD) == 0x01) &&
					    (STV090x_GETFIELD_Px(reg, DEMOD_DELOCK_FIELD) == 0x01))

						cpt_fail++;
				}
				if ((cpt_fail > 7) || (agc2_ovflw > 7))
					coarse_fail = 1;

				lock = 0;
			}
			k_ref -= 30;
		} while ((k_ref >= k_min) && (!lock) && (!coarse_fail));
	}

	return lock;

err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_chk_tmg(struct stv090x_state *state)
{
	u32 reg;
	s32 tmg_cpt, i;
	u8 freq, tmg_thh, tmg_thl;
	int tmg_lock;

	freq = STV090x_READ_DEMOD(state, CARFREQ);
	tmg_thh = STV090x_READ_DEMOD(state, TMGTHRISE);
	tmg_thl = STV090x_READ_DEMOD(state, TMGTHFALL);
	if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0x20) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0x00) < 0)
		goto err;

	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0x00); /* stop carrier offset search */
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, RTC, 0x80) < 0)
		goto err;

	if (STV090x_WRITE_DEMOD(state, RTCS2, 0x40) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x00) < 0)
		goto err;

	if (STV090x_WRITE_DEMOD(state, CFRINIT1, 0x00) < 0) /* set car ofset to 0 */
		goto err;
	if (STV090x_WRITE_DEMOD(state, CFRINIT0, 0x00) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x65) < 0)
		goto err;

	if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0) /* trigger acquisition */
		goto err;
	msleep(10);

	for (i = 0; i < 10; i++) {
		reg = STV090x_READ_DEMOD(state, DSTATUS);
		if (STV090x_GETFIELD_Px(reg, TMGLOCK_QUALITY_FIELD) >= 2)
			tmg_cpt++;
		msleep(1);
	}
	if (tmg_cpt >= 3)
		tmg_lock = 1;

	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, RTC, 0x88) < 0) /* DVB-S1 timing */
		goto err;
	if (STV090x_WRITE_DEMOD(state, RTCS2, 0x68) < 0) /* DVB-S2 timing */
		goto err;

	if (STV090x_WRITE_DEMOD(state, CARFREQ, freq) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGTHRISE, tmg_thh) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, TMGTHFALL, tmg_thl) < 0)
		goto err;

	return	tmg_lock;

err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_get_coldlock(struct stv090x_state *state, s32 timeout_dmd)
{
	struct dvb_frontend *fe = &state->frontend;

	u32 reg;
	s32 car_step, steps, cur_step, dir, freq, timeout_lock;
	int lock = 0;

	if (state->srate >= 10000000)
		timeout_lock = timeout_dmd / 3;
	else
		timeout_lock = timeout_dmd / 2;

	lock = stv090x_get_dmdlock(state, timeout_lock); /* cold start wait */
	if (!lock) {
		if (state->srate >= 10000000) {
			if (stv090x_chk_tmg(state)) {
				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0)
					goto err;
				lock = stv090x_get_dmdlock(state, timeout_dmd);
			} else {
				lock = 0;
			}
		} else {
			if (state->srate <= 4000000)
				car_step = 1000;
			else if (state->srate <= 7000000)
				car_step = 2000;
			else if (state->srate <= 10000000)
				car_step = 3000;
			else
				car_step = 5000;

			steps  = (state->search_range / 1000) / car_step;
			steps /= 2;
			steps  = 2 * (steps + 1);
			if (steps < 0)
				steps = 2;
			else if (steps > 12)
				steps = 12;

			cur_step = 1;
			dir = 1;

			if (!lock) {
				freq = state->frequency;
				state->tuner_bw = stv090x_car_width(state->srate, state->rolloff) + state->srate;
				while ((cur_step <= steps) && (!lock)) {
					if (dir > 0)
						freq += cur_step * car_step;
					else
						freq -= cur_step * car_step;

					/* Setup tuner */
					stv090x_i2c_gate_ctrl(fe, 1);

					if (state->config->tuner_set_frequency)
						state->config->tuner_set_frequency(fe, state->frequency);

					if (state->config->tuner_set_bandwidth)
						state->config->tuner_set_bandwidth(fe, state->tuner_bw);

					stv090x_i2c_gate_ctrl(fe, 0);

					msleep(50);

					stv090x_i2c_gate_ctrl(fe, 1);

					if (state->config->tuner_get_status)
						state->config->tuner_get_status(fe, &reg);

					if (reg)
						dprintk(FE_DEBUG, 1, "Tuner phase locked");
					else
						dprintk(FE_DEBUG, 1, "Tuner unlocked");

					stv090x_i2c_gate_ctrl(fe, 0);

					STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c);
					if (state->delsys == STV090x_DVBS2) {
						reg = STV090x_READ_DEMOD(state, DMDCFGMD);
						STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 0);
						STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
						if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
							goto err;
						STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
						STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
						if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
							goto err;
					}
					if (STV090x_WRITE_DEMOD(state, CFRINIT1, 0x00) < 0)
						goto err;
					if (STV090x_WRITE_DEMOD(state, CFRINIT0, 0x00) < 0)
						goto err;
					if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
						goto err;
					if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0)
						goto err;
					lock = stv090x_get_dmdlock(state, (timeout_dmd / 3));

					dir *= -1;
					cur_step++;
				}
			}
		}
	}

	return lock;

err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_get_loop_params(struct stv090x_state *state, s32 *freq_inc, s32 *timeout_sw, s32 *steps)
{
	s32 timeout, inc, steps_max, srate, car_max;

	srate = state->srate;
	car_max = state->search_range / 1000;
	car_max  = 65536 * (car_max / 2);
	car_max /= (state->mclk / 1000);

	if (car_max > 0x4000)
		car_max = 0x4000 ; /* maxcarrier should be<= +-1/4 Mclk */

	inc  = srate;
	inc /= state->mclk / 1000;
	inc *= 256;
	inc *= 256;
	inc /= 1000;

	switch (state->algo) {
	case STV090x_SEARCH_DVBS1:
	case STV090x_SEARCH_DSS:
		inc *= 3; /* freq step = 3% of srate */
		timeout = 20;
		break;

	case STV090x_SEARCH_DVBS2:
		inc *= 4;
		timeout = 25;
		break;

	case STV090x_SEARCH_AUTO:
	default:
		inc *= 3;
		timeout = 25;
		break;
	}
	inc /= 100;
	if ((inc > car_max) || (inc < 0))
		inc = car_max / 2; /* increment <= 1/8 Mclk */

	timeout *= 27500; /* 27.5 Msps reference */
	if (srate > 0)
		timeout /= (srate / 1000);

	if ((timeout > 100) || (timeout < 0))
		timeout = 100;

	steps_max = (car_max / inc) + 1; /* min steps = 3 */
	if ((steps_max > 100) || (steps_max < 0)) {
		steps_max = 100; /* max steps <= 100 */
		inc = car_max / steps_max;
	}
	*freq_inc = inc;
	*timeout_sw = timeout;
	*steps = steps_max;

	return 0;
}

static int stv090x_chk_signal(struct stv090x_state *state)
{
	s32 offst_car, agc2, car_max;
	int no_signal;

	offst_car  = STV090x_READ_DEMOD(state, CFR2) << 8;
	offst_car |= STV090x_READ_DEMOD(state, CFR1);

	agc2  = STV090x_READ_DEMOD(state, AGC2I1) << 8;
	agc2 |= STV090x_READ_DEMOD(state, AGC2I0);
	car_max = state->search_range / 1000;

	car_max += (car_max / 10); /* 10% margin */
	car_max  = (65536 * car_max / 2);
	car_max /= state->mclk / 1000;

	if (car_max > 0x4000)
		car_max = 0x4000;

	if ((agc2 > 0x2000) || (offst_car > 2 * car_max) || (offst_car < -2 * car_max)) {
		no_signal = 1;
		dprintk(FE_DEBUG, 1, "No Signal");
	} else {
		no_signal = 0;
		dprintk(FE_DEBUG, 1, "Found Signal");
	}

	return no_signal;
}

static int stv090x_search_car_loop(struct stv090x_state *state, s32 inc, s32 timeout, int zigzag, s32 steps_max)
{
	int no_signal, lock = 0;
	s32 cpt_step, offst_freq, car_max;
	u32 reg;

	car_max  = state->search_range / 1000;
	car_max += (car_max / 10);
	car_max  = (65536 * car_max / 2);
	car_max /= (state->mclk / 1000);
	if (car_max > 0x4000)
		car_max = 0x4000;

	if (zigzag)
		offst_freq = 0;
	else
		offst_freq = -car_max + inc;

	cpt_step = 0;
	do {
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, ((offst_freq / 256) & 0xff)) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, offst_freq & 0xff) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
			goto err;

		reg = STV090x_READ_DEMOD(state, PDELCTRL1);
		STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x1); /* stop DVB-S2 packet delin */
		if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			goto err;

		if (state->dev_ver == 0x12) {
			reg = STV090x_READ_DEMOD(state, TSCFGH);
			STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x1);
			if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
				goto err;
		}

		if (zigzag) {
			if (offst_freq >= 0)
				offst_freq = -offst_freq - 2 * inc;
			else
				offst_freq = -offst_freq;
		} else {
			offst_freq += 2 * inc;
		}

		lock = stv090x_get_dmdlock(state, timeout);
		no_signal = stv090x_chk_signal(state);

	} while ((!lock) &&
		 (!no_signal) &&
		  ((offst_freq - inc) < car_max) &&
		  ((offst_freq + inc) > -car_max) &&
		  (cpt_step < steps_max));

	reg = STV090x_READ_DEMOD(state, PDELCTRL1);
	STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0);
	if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			goto err;

	return lock;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_sw_algo(struct stv090x_state *state)
{
	int no_signal, zigzag, lock = 0;
	u32 reg;

	s32 dvbs2_fly_wheel;
	s32 inc, timeout_step, trials, steps_max;

	stv090x_get_loop_params(state, &inc, &timeout_step, &steps_max); /* get params */

	switch (state->algo) {
	case STV090x_SEARCH_DVBS1:
	case STV090x_SEARCH_DSS:
		/* accelerate the frequency detector */
		if (state->dev_ver >= 0x20) {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x3B) < 0)
				goto err;
		} else {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xef) < 0)
				goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0x49) < 0)
			goto err;
		zigzag = 0;
		break;

	case STV090x_SEARCH_DVBS2:
		if (state->dev_ver >= 0x20) {
			if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x79) < 0)
				goto err;
		} else {
			if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x68) < 0)
				goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0x89) < 0)
			goto err;
		zigzag = 1;
		break;

	case STV090x_SEARCH_AUTO:
	default:
		/* accelerate the frequency detector */
		if (state->dev_ver >= 0x20) {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x3b) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x79) < 0)
				goto err;
		} else {
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xef) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x68) < 0)
				goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0x69) < 0)
			goto err;
		zigzag = 0;
		break;
	}

	trials = 0;
	do {
		lock = stv090x_search_car_loop(state, inc, timeout_step, zigzag, steps_max);
		no_signal = stv090x_chk_signal(state);
		trials++;

		/*run the SW search 2 times maximum*/
		if (lock || no_signal || (trials == 2)) {
			/*Check if the demod is not losing lock in DVBS2*/
			if (state->dev_ver >= 0x20) {
				if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x49) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x9e) < 0)
					goto err;
			} else {
				if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xed) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x88) < 0)
					goto err;
			}

			reg = STV090x_READ_DEMOD(state, DMDSTATE);
			if ((lock) && (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == STV090x_DVBS2)) {
				/*Check if the demod is not losing lock in DVBS2*/
				msleep(timeout_step);
				reg = STV090x_READ_DEMOD(state, DMDFLYW);
				dvbs2_fly_wheel = STV090x_GETFIELD_Px(reg, FLYWHEEL_CPT_FIELD);
				if (dvbs2_fly_wheel < 0xd) {	 /*if correct frames is decrementing */
					msleep(timeout_step);
					reg = STV090x_READ_DEMOD(state, DMDFLYW);
					dvbs2_fly_wheel = STV090x_GETFIELD_Px(reg, FLYWHEEL_CPT_FIELD);
				}
				if (dvbs2_fly_wheel < 0xd) {
					/*FALSE lock, The demod is loosing lock */
					lock = 0;
					if (trials < 2) {
						if (state->dev_ver >= 0x20) {
							if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x79) < 0)
								goto err;
						} else {
							if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x68) < 0)
								goto err;
						}
						if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0x89) < 0)
							goto err;
					}
				}
			}
		}
	} while ((!lock) && (trials < 2) && (!no_signal));

	return lock;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static enum stv090x_delsys stv090x_get_std(struct stv090x_state *state)
{
	u32 reg;
	enum stv090x_delsys delsys;

	reg = STV090x_READ_DEMOD(state, DMDSTATE);
	if (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == 2)
		delsys = STV090x_DVBS2;
	else if (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == 3) {
		reg = STV090x_READ_DEMOD(state, FECM);
		if (STV090x_GETFIELD_Px(reg, DSS_DVB_FIELD) == 1)
			delsys = STV090x_DSS;
		else
			delsys = STV090x_DVBS1;
	} else {
		delsys = STV090x_ERROR;
	}

	return delsys;
}

/* in Hz */
static s32 stv090x_get_car_freq(struct stv090x_state *state, u32 mclk)
{
	s32 derot, int_1, int_2, tmp_1, tmp_2;
	u32 pow2;

	derot  = STV090x_READ_DEMOD(state, CFR2) << 16;
	derot |= STV090x_READ_DEMOD(state, CFR1) <<  8;
	derot |= STV090x_READ_DEMOD(state, CFR0);

	derot = comp2(derot, 24);
	pow2 = 1 << 12;
	int_1 = state->mclk / pow2;
	int_2 = derot / pow2;

	tmp_1 = state->mclk % pow2;
	tmp_2 = derot % pow2;

	derot = (int_1 * int_2) +
		((int_1 * tmp_2) / pow2) +
		((int_1 * tmp_1) / pow2);

	return derot;
}

static int stv090x_get_viterbi(struct stv090x_state *state)
{
	u32 reg, rate;

	reg = STV090x_READ_DEMOD(state, VITCURPUN);
	rate = STV090x_GETFIELD_Px(reg, VIT_CURPUN_FIELD);

	switch (rate) {
	case 13:
		state->fec = STV090x_PR12;
		break;

	case 18:
		state->fec = STV090x_PR23;
		break;

	case 21:
		state->fec = STV090x_PR34;
		break;

	case 24:
		state->fec = STV090x_PR56;
		break;

	case 25:
		state->fec = STV090x_PR67;
		break;

	case 26:
		state->fec = STV090x_PR78;
		break;

	default:
		state->fec = STV090x_PRERR;
		break;
	}

	return 0;
}

static enum stv090x_signal_state stv090x_get_sig_params(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;

	u8 tmg;
	u32 reg;
	s32 i = 0, offst_freq;

	msleep(5);

	if (state->algo == STV090x_BLIND_SEARCH) {
		tmg = STV090x_READ_DEMOD(state, TMGREG2);
		STV090x_WRITE_DEMOD(state, SFRSTEP, 0x5c);
		while ((i <= 50) && (!tmg) && (tmg != 0xff)) {
			tmg = STV090x_READ_DEMOD(state, TMGREG2);
			msleep(5);
			i += 5;
		}
	}
	state->delsys = stv090x_get_std(state);

	stv090x_i2c_gate_ctrl(fe, 1);

	if (state->config->tuner_get_frequency)
		state->config->tuner_get_frequency(fe, &state->frequency);

	stv090x_i2c_gate_ctrl(fe, 0);

	offst_freq = stv090x_get_car_freq(state, state->mclk) / 1000;
	state->frequency += offst_freq;
	stv090x_get_viterbi(state);
	reg = STV090x_READ_DEMOD(state, DMDMODCOD);
	state->modcod = STV090x_GETFIELD_Px(reg, DEMOD_MODCOD_FIELD);
	state->pilots = STV090x_GETFIELD_Px(reg, DEMOD_TYPE_FIELD) & 0x01;
	state->frame_len = STV090x_GETFIELD_Px(reg, DEMOD_TYPE_FIELD) >> 1;
	reg = STV090x_READ_DEMOD(state, TMGOBS);
	state->rolloff = STV090x_GETFIELD_Px(reg, ROLLOFF_STATUS_FIELD);
	reg = STV090x_READ_DEMOD(state, FECM);
	state->inversion = STV090x_GETFIELD_Px(reg, IQINV_FIELD);

	if ((state->algo == STV090x_BLIND_SEARCH) || (state->srate < 10000000)) {

		stv090x_i2c_gate_ctrl(fe, 1);

		if (state->config->tuner_get_frequency)
			state->config->tuner_get_frequency(fe, &state->frequency);

		stv090x_i2c_gate_ctrl(fe, 0);

		if (abs(offst_freq) <= ((state->search_range / 2000) + 500))
			return  STV090x_RANGEOK;
		else if (abs(offst_freq) <= (stv090x_car_width(state->srate, state->rolloff) / 2000))
			return STV090x_RANGEOK;
		else
			return STV090x_OUTOFRANGE; /* Out of Range */
	} else {
		if (abs(offst_freq) <= ((state->search_range / 2000) + 500))
			return STV090x_RANGEOK;
		else
			return STV090x_OUTOFRANGE;
	}

	return STV090x_OUTOFRANGE;
}

static u32 stv090x_get_tmgoffst(struct stv090x_state *state, u32 srate)
{
	s32 offst_tmg;
	s32 pow2;

	offst_tmg  = STV090x_READ_DEMOD(state, TMGREG2) << 16;
	offst_tmg |= STV090x_READ_DEMOD(state, TMGREG1) <<  8;
	offst_tmg |= STV090x_READ_DEMOD(state, TMGREG0);

	pow2 = 1 << 24;

	offst_tmg = comp2(offst_tmg, 24); /* 2's complement */
	if (!offst_tmg)
		offst_tmg = 1;

	offst_tmg  = ((s32) srate * 10) / (pow2 / offst_tmg);
	offst_tmg /= 320;

	return offst_tmg;
}

static u8 stv090x_optimize_carloop(struct stv090x_state *state, enum stv090x_modcod modcod, s32 pilots)
{
	u8 aclc = 0x29;
	s32 i;
	struct stv090x_long_frame_crloop *car_loop;

	if (state->dev_ver <= 0x12)
		car_loop = stv090x_s2_crl;
	else if (state->dev_ver == 0x20)
		car_loop = stv090x_s2_crl_cut20;
	else
		car_loop = stv090x_s2_crl;


	if (modcod < STV090x_QPSK_12) {
		i = 0;
		while ((i < 3) && (modcod != stv090x_s2_lowqpsk_crl_cut20[i].modcod))
			i++;

		if (i >= 3)
			i = 2;

	} else {
		i = 0;
		while ((i < 14) && (modcod != car_loop[i].modcod))
			i++;

		if (i >= 14) {
			i = 0;
			while ((i < 11) && (modcod != stv090x_s2_lowqpsk_crl_cut20[i].modcod))
				i++;

			if (i >= 11)
				i = 10;
		}
	}

	if (modcod <= STV090x_QPSK_25) {
		if (pilots) {
			if (state->srate <= 3000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_on_2;
			else if (state->srate <= 7000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_on_5;
			else if (state->srate <= 15000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_on_10;
			else if (state->srate <= 25000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_on_20;
			else
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_on_30;
		} else {
			if (state->srate <= 3000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_off_2;
			else if (state->srate <= 7000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_off_5;
			else if (state->srate <= 15000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_off_10;
			else if (state->srate <= 25000000)
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_off_20;
			else
				aclc = stv090x_s2_lowqpsk_crl_cut20[i].crl_pilots_off_30;
		}

	} else if (modcod <= STV090x_8PSK_910) {
		if (pilots) {
			if (state->srate <= 3000000)
				aclc = car_loop[i].crl_pilots_on_2;
			else if (state->srate <= 7000000)
				aclc = car_loop[i].crl_pilots_on_5;
			else if (state->srate <= 15000000)
				aclc = car_loop[i].crl_pilots_on_10;
			else if (state->srate <= 25000000)
				aclc = car_loop[i].crl_pilots_on_20;
			else
				aclc = car_loop[i].crl_pilots_on_30;
		} else {
			if (state->srate <= 3000000)
				aclc = car_loop[i].crl_pilots_off_2;
			else if (state->srate <= 7000000)
				aclc = car_loop[i].crl_pilots_off_5;
			else if (state->srate <= 15000000)
				aclc = car_loop[i].crl_pilots_off_10;
			else if (state->srate <= 25000000)
				aclc = car_loop[i].crl_pilots_off_20;
			else
				aclc = car_loop[i].crl_pilots_off_30;
		}
	} else { /* 16APSK and 32APSK */
		if (state->srate <= 3000000)
			aclc = stv090x_s2_apsk_crl_cut20[i].crl_pilots_on_2;
		else if (state->srate <= 7000000)
			aclc = stv090x_s2_apsk_crl_cut20[i].crl_pilots_on_5;
		else if (state->srate <= 15000000)
			aclc = stv090x_s2_apsk_crl_cut20[i].crl_pilots_on_10;
		else if (state->srate <= 25000000)
			aclc = stv090x_s2_apsk_crl_cut20[i].crl_pilots_on_20;
		else
			aclc = stv090x_s2_apsk_crl_cut20[i].crl_pilots_on_30;
	}

	return aclc;
}

static u8 stv090x_optimize_carloop_short(struct stv090x_state *state)
{
	s32 index = 0;
	u8 aclc = 0x0b;

	switch (state->modulation) {
	case STV090x_QPSK:
	default:
		index = 0;
		break;
	case STV090x_8PSK:
		index = 1;
		break;
	case STV090x_16APSK:
		index = 2;
		break;
	case STV090x_32APSK:
		index = 3;
		break;
	}

	switch (state->dev_ver) {
	case 0x20:
		if (state->srate <= 3000000)
			aclc = stv090x_s2_short_crl[index].crl_cut20_2;
		else if (state->srate <= 7000000)
			aclc = stv090x_s2_short_crl[index].crl_cut20_5;
		else if (state->srate <= 15000000)
			aclc = stv090x_s2_short_crl[index].crl_cut20_10;
		else if (state->srate <= 25000000)
			aclc = stv090x_s2_short_crl[index].crl_cut20_20;
		else
			aclc = stv090x_s2_short_crl[index].crl_cut20_30;
		break;

	case 0x12:
	default:
		if (state->srate <= 3000000)
			aclc = stv090x_s2_short_crl[index].crl_cut12_2;
		else if (state->srate <= 7000000)
			aclc = stv090x_s2_short_crl[index].crl_cut12_5;
		else if (state->srate <= 15000000)
			aclc = stv090x_s2_short_crl[index].crl_cut12_10;
		else if (state->srate <= 25000000)
			aclc = stv090x_s2_short_crl[index].crl_cut12_20;
		else
			aclc = stv090x_s2_short_crl[index].crl_cut12_30;
		break;
	}

	return aclc;
}

static int stv090x_optimize_track(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;

	enum stv090x_rolloff rolloff;
	enum stv090x_modcod modcod;

	s32 srate, pilots, aclc, f_1, f_0, i = 0, blind_tune = 0;
	u32 reg;

	srate  = stv090x_get_srate(state, state->mclk);
	srate += stv090x_get_tmgoffst(state, srate);

	switch (state->delsys) {
	case STV090x_DVBS1:
	case STV090x_DSS:
		if (state->algo == STV090x_SEARCH_AUTO) {
			reg = STV090x_READ_DEMOD(state, DMDCFGMD);
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
				goto err;
		}
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, ROLLOFF_CONTROL_FIELD, state->rolloff);
		STV090x_SETFIELD_Px(reg, MANUAL_ROLLOFF_FIELD, 0x01);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x75) < 0)
			goto err;
		break;

	case STV090x_DVBS2:
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
		STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, ACLC, 0) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, BCLC, 0) < 0)
			goto err;
		if (state->frame_len == STV090x_LONG_FRAME) {
			reg = STV090x_READ_DEMOD(state, DMDMODCOD);
			modcod = STV090x_GETFIELD_Px(reg, DEMOD_MODCOD_FIELD);
			pilots = STV090x_GETFIELD_Px(reg, DEMOD_TYPE_FIELD) & 0x01;
			aclc = stv090x_optimize_carloop(state, modcod, pilots);
			if (modcod <= STV090x_QPSK_910) {
				STV090x_WRITE_DEMOD(state, ACLC2S2Q, aclc);
			} else if (modcod <= STV090x_8PSK_910) {
				if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, ACLC2S28, aclc) < 0)
					goto err;
			}
			if ((state->demod_mode == STV090x_SINGLE) && (modcod > STV090x_8PSK_910)) {
				if (modcod <= STV090x_16APSK_910) {
					if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
						goto err;
					if (STV090x_WRITE_DEMOD(state, ACLC2S216A, aclc) < 0)
						goto err;
				} else {
					if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
						goto err;
					if (STV090x_WRITE_DEMOD(state, ACLC2S232A, aclc) < 0)
						goto err;
				}
			}
		} else {
			/*Carrier loop setting for short frame*/
			aclc = stv090x_optimize_carloop_short(state);
			if (state->modulation == STV090x_QPSK) {
				if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, aclc) < 0)
					goto err;
			} else if (state->modulation == STV090x_8PSK) {
				if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, ACLC2S28, aclc) < 0)
					goto err;
			} else if (state->modulation == STV090x_16APSK) {
				if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, ACLC2S216A, aclc) < 0)
					goto err;
			} else if (state->modulation == STV090x_32APSK)  {
				if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, ACLC2S232A, aclc) < 0)
					goto err;
			}
		}
		if (state->dev_ver <= 0x11) {
			if (state->demod_mode != STV090x_SINGLE)
				stv090x_activate_modcod(state); /* link to LDPC after demod LOCK */
		}
		STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x67); /* PER */
		break;

	case STV090x_UNKNOWN:
	default:
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
		STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;
		break;
	}

	f_1 = STV090x_READ_DEMOD(state, CFR2);
	f_0 = STV090x_READ_DEMOD(state, CFR1);
	reg = STV090x_READ_DEMOD(state, TMGOBS);
	rolloff = STV090x_GETFIELD_Px(reg, ROLLOFF_STATUS_FIELD);

	if (state->algo == STV090x_BLIND_SEARCH) {
		STV090x_WRITE_DEMOD(state, SFRSTEP, 0x00);
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 0x00);
		STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0x01) < 0)
			goto err;
		stv090x_set_srate(state, srate);
		stv090x_set_max_srate(state, state->mclk, srate);
		stv090x_set_min_srate(state, state->mclk, srate);
		blind_tune = 1;
	}

	if (state->dev_ver >= 0x20) {
		if ((state->search_mode == STV090x_SEARCH_DVBS1)	||
		    (state->search_mode == STV090x_SEARCH_DSS)		||
		    (state->search_mode == STV090x_SEARCH_AUTO)) {

			if (STV090x_WRITE_DEMOD(state, VAVSRVIT, 0x0a) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, VITSCALE, 0x00) < 0)
				goto err;
		}
	}

	if (state->dev_ver < 0x20) {
		if (STV090x_WRITE_DEMOD(state, CARHDR, 0x08) < 0)
			goto err;
	}
	if (state->dev_ver == 0x10) {
		if (STV090x_WRITE_DEMOD(state, CORRELEXP, 0x0a) < 0)
			goto err;
	}

	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
		goto err;

	if ((state->dev_ver >= 0x20) || (blind_tune == 1) || (state->srate < 10000000)) {

		if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_0) < 0)
			goto err;
		state->tuner_bw = stv090x_car_width(srate, state->rolloff) + 10000000;

		if ((state->dev_ver >= 0x20) || (blind_tune == 1)) {

			if (state->algo != STV090x_WARM_SEARCH) {

				stv090x_i2c_gate_ctrl(fe, 1);

				if (state->config->tuner_set_bandwidth)
					state->config->tuner_set_bandwidth(fe, state->tuner_bw);

				stv090x_i2c_gate_ctrl(fe, 0);

			}
		}
		if ((state->algo == STV090x_BLIND_SEARCH) || (state->srate < 10000000))
			msleep(50); /* blind search: wait 50ms for SR stabilization */
		else
			msleep(5);

		stv090x_get_lock_tmg(state);

		if (!(stv090x_get_dmdlock(state, (state->DemodTimeout / 2)))) {
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_0) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
				goto err;

			i = 0;

			while ((!(stv090x_get_dmdlock(state, (state->DemodTimeout / 2)))) && (i <= 2)) {

				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_0) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
					goto err;
				i++;
			}
		}

	}

	if (state->dev_ver >= 0x20) {
		if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x49) < 0)
			goto err;
	}
	if ((state->delsys == STV090x_DVBS1) || (state->delsys == STV090x_DSS))
		stv090x_set_vit_thtracq(state);

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_get_feclock(struct stv090x_state *state, s32 timeout)
{
	s32 timer = 0, lock = 0, stat;
	u32 reg;

	while ((timer < timeout) && (!lock)) {
		reg = STV090x_READ_DEMOD(state, DMDSTATE);
		stat = STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD);

		switch (stat) {
		case 0: /* searching */
		case 1: /* first PLH detected */
		default:
			lock = 0;
			break;

		case 2: /* DVB-S2 mode */
			reg = STV090x_READ_DEMOD(state, PDELSTATUS1);
			lock = STV090x_GETFIELD_Px(reg, PKTDELIN_LOCK_FIELD);
			break;

		case 3: /* DVB-S1/legacy mode */
			reg = STV090x_READ_DEMOD(state, VSTATUSVIT);
			lock = STV090x_GETFIELD_Px(reg, LOCKEDVIT_FIELD);
			break;
		}
		if (!lock) {
			msleep(10);
			timer += 10;
		}
	}
	return lock;
}

static int stv090x_get_lock(struct stv090x_state *state, s32 timeout_dmd, s32 timeout_fec)
{
	u32 reg;
	s32 timer = 0;
	int lock;

	lock = stv090x_get_dmdlock(state, timeout_dmd);
	if (lock)
		lock = stv090x_get_feclock(state, timeout_fec);

	if (lock) {
		lock = 0;

		while ((timer < timeout_fec) && (!lock)) {
			reg = STV090x_READ_DEMOD(state, TSSTATUS);
			lock = STV090x_GETFIELD_Px(reg, TSFIFO_LINEOK_FIELD);
			msleep(1);
			timer++;
		}
	}

	return lock;
}

static int stv090x_set_s2rolloff(struct stv090x_state *state)
{
	s32 rolloff;
	u32 reg;

	if (state->dev_ver == 0x10) {
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, MANUAL_ROLLOFF_FIELD, 0x01);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
			goto err;
		rolloff = STV090x_READ_DEMOD(state, MATSTR1) & 0x03;
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, ROLLOFF_CONTROL_FIELD, reg);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
			goto err;
	} else {
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, MANUAL_ROLLOFF_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
			goto err;
	}
	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static enum stv090x_signal_state stv090x_acq_fixs1(struct stv090x_state *state)
{
	s32 srate, f_1, f_2;
	enum stv090x_signal_state signal_state = STV090x_NODATA;
	u32 reg;
	int lock;

	reg = STV090x_READ_DEMOD(state, DMDSTATE);
	if (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == 3) { /* DVB-S mode */
		srate  = stv090x_get_srate(state, state->mclk);
		srate += stv090x_get_tmgoffst(state, state->srate);

		if (state->algo == STV090x_BLIND_SEARCH)
			stv090x_set_srate(state, state->srate);

		stv090x_get_lock_tmg(state);

		f_1 = STV090x_READ_DEMOD(state, CFR2);
		f_2 = STV090x_READ_DEMOD(state, CFR1);

		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			goto err;

		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, SPECINV_CONTROL_FIELD, STV090x_IQ_SWAP);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c) < 0) /* stop demod */
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_2) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0) /* warm start trigger */
			goto err;

		if (stv090x_get_lock(state, state->DemodTimeout, state->FecTimeout)) {
			lock = 1;
			stv090x_get_sig_params(state);
			stv090x_optimize_track(state);
		} else {
			reg = STV090x_READ_DEMOD(state, DEMOD);
			STV090x_SETFIELD_Px(reg, SPECINV_CONTROL_FIELD, STV090x_IQ_NORMAL);
			if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_2) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0) /* warm start trigger */
				goto err;
			if (stv090x_get_lock(state, state->DemodTimeout, state->FecTimeout)) {
				lock = 1;
				signal_state = stv090x_get_sig_params(state);
				stv090x_optimize_track(state);
			}
		}
	} else {
		lock = 0;
	}

	return signal_state;

err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static enum stv090x_signal_state stv090x_algo(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;
	enum stv090x_signal_state signal_state = STV090x_NOCARRIER;
	u32 reg;
	s32 timeout_dmd = 500, timeout_fec = 50;
	int lock = 0, low_sr, no_signal = 0;

	reg = STV090x_READ_DEMOD(state, TSCFGH);
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 1); /* Stop path 1 stream merger */
	if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
		goto err;

	if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x5c) < 0) /* Demod stop */
		goto err;

	if (state->dev_ver >= 0x20) {
		if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x9e) < 0) /* cut 2.0 */
			goto err;
	} else {
		if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x88) < 0) /* cut 1.x */
			goto err;
	}

	stv090x_get_lock_tmg(state);

	if (state->algo == STV090x_BLIND_SEARCH) {
		state->tuner_bw = 2 * 36000000; /* wide bw for unknown srate */
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0x00) < 0) /* wider srate scan */
			goto err;
		stv090x_set_srate(state, 1000000); /* inital srate = 1Msps */
	} else {
		/* known srate */
		if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x20) < 0)
			goto err;
		if (STV090x_WRITE_DEMOD(state, TMGCFG, 0xd2) < 0)
			goto err;

		if (state->srate >= 10000000) {
			if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0) /* High SR */
				goto err;
		} else {
			if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x60) < 0) /* Low SR */
				goto err;
		}

		if (state->dev_ver >= 0x20) {
			if (STV090x_WRITE_DEMOD(state, KREFTMG, 0x5a) < 0)
				goto err;
			if (state->algo == STV090x_COLD_SEARCH)
				state->tuner_bw = (15 * (stv090x_car_width(state->srate, state->rolloff) + 1000000)) / 10;
			else if (state->algo == STV090x_WARM_SEARCH)
				state->tuner_bw = stv090x_car_width(state->srate, state->rolloff) + 10000000;
		} else {
			if (STV090x_WRITE_DEMOD(state, KREFTMG, 0xc1) < 0)
				goto err;
			state->tuner_bw = (15 * (stv090x_car_width(state->srate, state->rolloff) + 10000000)) / 10;
		}
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0x01) < 0) /* narrow srate scan */
			goto err;
		stv090x_set_srate(state, state->srate);
		stv090x_set_max_srate(state, state->mclk, state->srate);
		stv090x_set_min_srate(state, state->mclk, state->srate);

		if (state->srate >= 10000000)
			low_sr = 1;
	}

	/* Setup tuner */
	stv090x_i2c_gate_ctrl(fe, 1);

	if (state->config->tuner_set_bbgain)
		state->config->tuner_set_bbgain(fe, 10); /* 10dB */

	if (state->config->tuner_set_frequency)
		state->config->tuner_set_frequency(fe, state->frequency);

	if (state->config->tuner_set_bandwidth)
		state->config->tuner_set_bandwidth(fe, state->tuner_bw);

	stv090x_i2c_gate_ctrl(fe, 0);

	msleep(50);

	stv090x_i2c_gate_ctrl(fe, 1);

	if (state->config->tuner_get_status)
		state->config->tuner_get_status(fe, &reg);

	if (reg)
		dprintk(FE_DEBUG, 1, "Tuner phase locked");
	else
		dprintk(FE_DEBUG, 1, "Tuner unlocked");

	stv090x_i2c_gate_ctrl(fe, 0);

	reg = STV090x_READ_DEMOD(state, DEMOD);
	STV090x_SETFIELD_Px(reg, SPECINV_CONTROL_FIELD, state->inversion);
	STV090x_SETFIELD_Px(reg, MANUAL_ROLLOFF_FIELD, 1);
	if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
		goto err;
	stv090x_delivery_search(state);
	if (state->algo == STV090x_BLIND_SEARCH)
		stv090x_start_search(state);

	if (state->dev_ver == 0x12) {
		reg = STV090x_READ_DEMOD(state, TSCFGH);
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
			goto err;
		msleep(3);
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 1); /* merger reset */
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
			goto err;
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
			goto err;
	}

	if (state->algo == STV090x_BLIND_SEARCH)
		lock = stv090x_blind_search(state);
	else if (state->algo == STV090x_COLD_SEARCH)
		lock = stv090x_get_coldlock(state, timeout_dmd);
	else if (state->algo == STV090x_WARM_SEARCH)
		lock = stv090x_get_dmdlock(state, timeout_dmd);

	if ((!lock) && (state->algo == STV090x_COLD_SEARCH)) {
		if (!low_sr) {
			if (stv090x_chk_tmg(state))
				lock = stv090x_sw_algo(state);
		}
	}

	if (lock)
		signal_state = stv090x_get_sig_params(state);

	if ((lock) && (signal_state == STV090x_RANGEOK)) { /* signal within Range */
		stv090x_optimize_track(state);
		if (state->dev_ver <= 0x11) { /*workaround for dual DVBS1 cut 1.1 and 1.0 only*/
			if (stv090x_get_std(state) == STV090x_DVBS1) {
				msleep(20);
				reg = STV090x_READ_DEMOD(state, TSCFGH);
				STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
				if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
					goto err;
			} else {
				reg = STV090x_READ_DEMOD(state, TSCFGH);
				STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
				if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
					goto err;
				msleep(3);
				STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 1); /* merger reset */
				if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
					goto err;
				STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
				if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
					goto err;
			}
		} else if (state->dev_ver == 0x20) { /*cut 2.0 :release TS reset after demod lock and TrackingOptimization*/
			reg = STV090x_READ_DEMOD(state, TSCFGH);
			STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
			if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
				goto err;
			msleep(3);
			STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 1); /* merger reset */
			if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
				goto err;

			STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
			if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
				goto err;
		}

		if (stv090x_get_lock(state, timeout_fec, timeout_fec)) {
			lock = 1;
			if (state->delsys == STV090x_DVBS2) {
				stv090x_set_s2rolloff(state);
				if (STV090x_WRITE_DEMOD(state, PDELCTRL2, 0x40) < 0)
					goto err;
				if (STV090x_WRITE_DEMOD(state, PDELCTRL2, 0x00) < 0) /* RESET counter */
					goto err;
				if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x67) < 0) /* PER */
					goto err;
			} else {
				if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x75) < 0)
					goto err;
			}
			if (STV090x_WRITE_DEMOD(state, FBERCPT4, 0x00) < 0)
				goto err;
			if (STV090x_WRITE_DEMOD(state, ERRCTRL2, 0xc1) < 0)
				goto err;
		} else {
			lock = 0;
			signal_state = STV090x_NODATA;
			no_signal = stv090x_chk_signal(state);
		}
	}
	if ((signal_state == STV090x_NODATA) && (!no_signal)) {
		if (state->dev_ver <= 0x11) {
			reg = STV090x_READ_DEMOD(state, DMDSTATE);
			if (((STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD)) == STV090x_DVBS2) && (state->inversion == INVERSION_AUTO))
				signal_state = stv090x_acq_fixs1(state);
		}
	}
	return signal_state;

err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static enum dvbfe_search stv090x_search(struct dvb_frontend *fe, struct dvb_frontend_parameters *p)
{
	struct stv090x_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *props = &fe->dtv_property_cache;

	state->delsys = props->delivery_system;
	state->frequency = p->frequency;
	state->srate = p->u.qpsk.symbol_rate;

	if (!stv090x_algo(state)) {
		dprintk(FE_DEBUG, 1, "Search success!");
		return DVBFE_ALGO_SEARCH_SUCCESS;
	} else {
		dprintk(FE_DEBUG, 1, "Search failed!");
		return DVBFE_ALGO_SEARCH_FAILED;
	}

	return DVBFE_ALGO_SEARCH_ERROR;
}

/* FIXME! */
static int stv090x_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;
	u8 search_state;
	int locked = 0;

	reg = STV090x_READ_DEMOD(state, DMDSTATE);
	search_state = STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD);

	switch (search_state) {
	case 0: /* searching */
	case 1: /* first PLH detected */
	default:
		dprintk(FE_DEBUG, 1, "Status: Unlocked (Searching ..)");
		locked = 0;
		break;

	case 2: /* DVB-S2 mode */
		dprintk(FE_DEBUG, 1, "Delivery system: DVB-S2");
		reg = STV090x_READ_DEMOD(state, DSTATUS);
		if (STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD)) {
			reg = STV090x_READ_DEMOD(state, TSSTATUS);
			if (STV090x_GETFIELD_Px(reg, TSFIFO_LINEOK_FIELD)) {
				locked = 1;
				*status = FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
			}
		}
		break;

	case 3: /* DVB-S1/legacy mode */
		dprintk(FE_DEBUG, 1, "Delivery system: DVB-S");
		reg = STV090x_READ_DEMOD(state, DSTATUS);
		if (STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD)) {
			reg = STV090x_READ_DEMOD(state, VSTATUSVIT);
			if (STV090x_GETFIELD_Px(reg, LOCKEDVIT_FIELD)) {
				reg = STV090x_READ_DEMOD(state, TSSTATUS);
				if (STV090x_GETFIELD_Px(reg, TSFIFO_LINEOK_FIELD)) {
					locked = 1;
					*status = FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
				}
			}
		}
		break;
	}

	return locked;
}

static int stv090x_read_per(struct dvb_frontend *fe, u32 *per)
{
	struct stv090x_state *state = fe->demodulator_priv;

	s32 count_4, count_3, count_2, count_1, count_0, count;
	u32 reg, h, m, l;
	enum fe_status status;

	if (!stv090x_read_status(fe, &status)) {
		*per = 1 << 23; /* Max PER */
	} else {
		/* Counter 2 */
		reg = STV090x_READ_DEMOD(state, ERRCNT22);
		h = STV090x_GETFIELD_Px(reg, ERR_CNT2_FIELD);

		reg = STV090x_READ_DEMOD(state, ERRCNT21);
		m = STV090x_GETFIELD_Px(reg, ERR_CNT21_FIELD);

		reg = STV090x_READ_DEMOD(state, ERRCNT20);
		l = STV090x_GETFIELD_Px(reg, ERR_CNT20_FIELD);

		*per = ((h << 16) | (m << 8) | l);

		count_4 = STV090x_READ_DEMOD(state, FBERCPT4);
		count_3 = STV090x_READ_DEMOD(state, FBERCPT3);
		count_2 = STV090x_READ_DEMOD(state, FBERCPT2);
		count_1 = STV090x_READ_DEMOD(state, FBERCPT1);
		count_0 = STV090x_READ_DEMOD(state, FBERCPT0);

		if ((!count_4) && (!count_3)) {
			count  = (count_2 & 0xff) << 16;
			count |= (count_1 & 0xff) <<  8;
			count |=  count_0 & 0xff;
		} else {
			count = 1 << 24;
		}
		if (count == 0)
			*per = 1;
	}
	if (STV090x_WRITE_DEMOD(state, FBERCPT4, 0) < 0)
		goto err;
	if (STV090x_WRITE_DEMOD(state, ERRCTRL2, 0xc1) < 0)
		goto err;

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_table_lookup(const struct stv090x_tab *tab, int max, int val)
{
	int res = 0;
	int min = 0, med;

	if (val < tab[min].read)
		res = tab[min].real;
	else if (val >= tab[max].read)
		res = tab[max].real;
	else {
		while ((max - min) > 1) {
			med = (max + min) / 2;
			if (val >= tab[min].read && val < tab[med].read)
				max = med;
			else
				min = med;
		}
		res = ((val - tab[min].read) *
		       (tab[max].real - tab[min].real) /
		       (tab[max].read - tab[min].read)) +
			tab[min].real;
	}

	return res;
}

static int stv090x_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;
	s32 agc;

	reg = STV090x_READ_DEMOD(state, AGCIQIN1);
	agc = STV090x_GETFIELD_Px(reg, AGCIQ_VALUE_FIELD);

	*strength = stv090x_table_lookup(stv090x_rf_tab, ARRAY_SIZE(stv090x_rf_tab) - 1, agc);
	if (agc > stv090x_rf_tab[0].read)
		*strength = 5;
	else if (agc < stv090x_rf_tab[ARRAY_SIZE(stv090x_rf_tab) - 1].read)
		*strength = -100;

	return 0;
}

static int stv090x_read_cnr(struct dvb_frontend *fe, u16 *cnr)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg_0, reg_1, reg, i;
	s32 val_0, val_1, val = 0;
	u8 lock_f;

	switch (state->delsys) {
	case STV090x_DVBS2:
		reg = STV090x_READ_DEMOD(state, DSTATUS);
		lock_f = STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD);
		if (lock_f) {
			msleep(5);
			for (i = 0; i < 16; i++) {
				reg_1 = STV090x_READ_DEMOD(state, NNOSPLHT1);
				val_1 = STV090x_GETFIELD_Px(reg_1, NOSPLHT_NORMED_FIELD);
				reg_0 = STV090x_READ_DEMOD(state, NNOSPLHT0);
				val_0 = STV090x_GETFIELD_Px(reg_1, NOSPLHT_NORMED_FIELD);
				val  += MAKEWORD16(val_1, val_0);
				msleep(1);
			}
			val /= 16;
			*cnr = stv090x_table_lookup(stv090x_s2cn_tab, ARRAY_SIZE(stv090x_s2cn_tab) - 1, val);
			if (val < stv090x_s2cn_tab[ARRAY_SIZE(stv090x_s2cn_tab) - 1].read)
				*cnr = 1000;
		}
		break;

	case STV090x_DVBS1:
	case STV090x_DSS:
		reg = STV090x_READ_DEMOD(state, DSTATUS);
		lock_f = STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD);
		if (lock_f) {
			msleep(5);
			for (i = 0; i < 16; i++) {
				reg_1 = STV090x_READ_DEMOD(state, NOSDATAT1);
				val_1 = STV090x_GETFIELD_Px(reg_1, NOSDATAT_UNNORMED_FIELD);
				reg_0 = STV090x_READ_DEMOD(state, NOSDATAT0);
				val_0 = STV090x_GETFIELD_Px(reg_1, NOSDATAT_UNNORMED_FIELD);
				val  += MAKEWORD16(val_1, val_0);
				msleep(1);
			}
			val /= 16;
			*cnr = stv090x_table_lookup(stv090x_s1cn_tab, ARRAY_SIZE(stv090x_s1cn_tab) - 1, val);
			if (val < stv090x_s2cn_tab[ARRAY_SIZE(stv090x_s1cn_tab) - 1].read)
				*cnr = 1000;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int stv090x_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	reg = STV090x_READ_DEMOD(state, DISTXCTL);
	switch (tone) {
	case SEC_TONE_ON:
		STV090x_SETFIELD_Px(reg, DISTX_MODE_FIELD, 0);
		STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
			goto err;
		STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
			goto err;
		break;

	case SEC_TONE_OFF:
		STV090x_SETFIELD_Px(reg, DISTX_MODE_FIELD, 0);
		STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
			goto err;
		break;
	default:
		return -EINVAL;
	}

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}


static enum dvbfe_algo stv090x_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

static int stv090x_send_diseqc_msg(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg, idle = 0, fifo_full = 1;
	int i;

	reg = STV090x_READ_DEMOD(state, DISTXCTL);
	STV090x_SETFIELD_Px(reg, DIS_PRECHARGE_FIELD, 1);
	if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		goto err;

	for (i = 0; i < cmd->msg_len; i++) {

		while (fifo_full) {
			reg = STV090x_READ_DEMOD(state, DISTXSTATUS);
			fifo_full = STV090x_GETFIELD_Px(reg, FIFO_FULL_FIELD);
		}

		if (STV090x_WRITE_DEMOD(state, DISTXDATA, cmd->msg[i]) < 0)
			goto err;
		i++;
	}
	reg = STV090x_READ_DEMOD(state, DISTXCTL);
	STV090x_SETFIELD_Px(reg, DIS_PRECHARGE_FIELD, 0);
	if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		goto err;

	i = 0;

	while ((!idle) && (i < 10)) {
		reg = STV090x_READ_DEMOD(state, DISTXSTATUS);
		idle = STV090x_GETFIELD_Px(reg, TX_IDLE_FIELD);
		msleep(10);
		i++;
	}

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_recv_slave_reply(struct dvb_frontend *fe, struct dvb_diseqc_slave_reply *reply)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg = 0, i = 0, rx_end = 0;

	while ((rx_end != 1) && (i < 10)) {
		msleep(10);
		i++;
		reg = STV090x_READ_DEMOD(state, DISRX_ST0);
		rx_end = STV090x_GETFIELD_Px(reg, RX_END_FIELD);
	}

	if (rx_end) {
		reply->msg_len = STV090x_GETFIELD_Px(reg, FIFO_BYTENBR_FIELD);
		for (i = 0; i < reply->msg_len; i++)
			reply->msg[i] = STV090x_READ_DEMOD(state, DISRXDATA);
	}

	return 0;
}

static int stv090x_sleep(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	dprintk(FE_DEBUG, 1, "Set %s to sleep",
		state->device == STV0900 ? "STV0900" : "STV0903");

	reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
	STV090x_SETFIELD(reg, STANDBY_FIELD, 0x01);
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, reg) < 0)
		goto err;

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_wakeup(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	dprintk(FE_DEBUG, 1, "Wake %s from standby",
		state->device == STV0900 ? "STV0900" : "STV0903");

	reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
	STV090x_SETFIELD(reg, STANDBY_FIELD, 0x00);
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, reg) < 0)
		goto err;

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static void stv090x_release(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;

	kfree(state);
}

static int stv090x_ldpc_mode(struct stv090x_state *state, enum stv090x_mode ldpc_mode)
{
	u32 reg;

	switch (ldpc_mode) {
	case STV090x_DUAL:
	default:
		reg = stv090x_read_reg(state, STV090x_GENCFG);
		if ((state->demod_mode != STV090x_DUAL) || (STV090x_GETFIELD(reg, DDEMOD_FIELD) != 1)) {
			/* follow LDPC default state */
			if (stv090x_write_reg(state, STV090x_GENCFG, reg) < 0)
				goto err;
			state->demod_mode = STV090x_DUAL;
			reg = stv090x_read_reg(state, STV090x_TSTRES0);
			STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x1);
			if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
				goto err;
			STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x0);
			if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
				goto err;
		}
		break;

	case STV090x_SINGLE:
		if (state->demod == STV090x_DEMODULATOR_1) {
			if (stv090x_write_reg(state, STV090x_GENCFG, 0x06) < 0) /* path 2 */
				goto err;
		} else {
			if (stv090x_write_reg(state, STV090x_GENCFG, 0x04) < 0) /* path 1 */
				goto err;
		}

		reg = stv090x_read_reg(state, STV090x_TSTRES0);
		STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x1);
		if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
			goto err;
		STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x0);
		if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
			goto err;

		reg = STV090x_READ_DEMOD(state, PDELCTRL1);
		STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x01);
		if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			goto err;
		STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			goto err;
		break;
	}

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

/* return (Hz), clk in Hz*/
static u32 stv090x_get_mclk(struct stv090x_state *state)
{
	const struct stv090x_config *config = state->config;
	u32 div, reg;
	u8 ratio;

	div = stv090x_read_reg(state, STV090x_NCOARSE);
	reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
	ratio = STV090x_GETFIELD(reg, SELX1RATIO_FIELD) ? 4 : 6;

	return (div + 1) * config->xtal / ratio; /* kHz */
}

static int stv090x_set_mclk(struct stv090x_state *state, u32 mclk, u32 clk)
{
	const struct stv090x_config *config = state->config;
	u32 reg, div, clk_sel;

	reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
	clk_sel = ((STV090x_GETFIELD(reg, SELX1RATIO_FIELD) == 1) ? 4 : 6);

	div = ((clk_sel * mclk) / config->xtal) - 1;

	reg = stv090x_read_reg(state, STV090x_NCOARSE);
	STV090x_SETFIELD(reg, M_DIV_FIELD, div);
	if (stv090x_write_reg(state, STV090x_NCOARSE, reg) < 0)
		goto err;

	state->mclk = stv090x_get_mclk(state);

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_set_tspath(struct stv090x_state *state)
{
	u32 reg;

	if (state->dev_ver >= 0x20) {
		switch (state->config->ts1_mode) {
		case STV090x_TSMODE_PARALLEL_PUNCTURED:
		case STV090x_TSMODE_DVBCI:
			switch (state->config->ts2_mode) {
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			default:
				stv090x_write_reg(state, STV090x_TSGENERAL, 0x00);
				break;

			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
				if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x06) < 0) /* Mux'd stream mode */
					goto err;
				reg = stv090x_read_reg(state, STV090x_P1_TSCFGM);
				STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
				if (stv090x_write_reg(state, STV090x_P1_TSCFGM, reg) < 0)
					goto err;
				reg = stv090x_read_reg(state, STV090x_P2_TSCFGM);
				STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
				if (stv090x_write_reg(state, STV090x_P2_TSCFGM, reg) < 0)
					goto err;
				if (stv090x_write_reg(state, STV090x_P1_TSSPEED, 0x14) < 0)
					goto err;
				if (stv090x_write_reg(state, STV090x_P2_TSSPEED, 0x28) < 0)
					goto err;
				break;
			}
			break;

		case STV090x_TSMODE_SERIAL_PUNCTURED:
		case STV090x_TSMODE_SERIAL_CONTINUOUS:
		default:
			switch (state->config->ts2_mode) {
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			default:
				if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x0c) < 0)
					goto err;
				break;

			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
				if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x0a) < 0)
					goto err;
				break;
			}
			break;
		}
	} else {
		switch (state->config->ts1_mode) {
		case STV090x_TSMODE_PARALLEL_PUNCTURED:
		case STV090x_TSMODE_DVBCI:
			switch (state->config->ts2_mode) {
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			default:
				stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x10);
				break;

			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
				stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x16);
				reg = stv090x_read_reg(state, STV090x_P1_TSCFGM);
				STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
				if (stv090x_write_reg(state, STV090x_P1_TSCFGM, reg) < 0)
					goto err;
				reg = stv090x_read_reg(state, STV090x_P1_TSCFGM);
				STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 0);
				if (stv090x_write_reg(state, STV090x_P1_TSCFGM, reg) < 0)
					goto err;
				if (stv090x_write_reg(state, STV090x_P1_TSSPEED, 0x14) < 0)
					goto err;
				if (stv090x_write_reg(state, STV090x_P2_TSSPEED, 0x28) < 0)
					goto err;
				break;
			}
			break;

		case STV090x_TSMODE_SERIAL_PUNCTURED:
		case STV090x_TSMODE_SERIAL_CONTINUOUS:
		default:
			switch (state->config->ts2_mode) {
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			default:
				stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x14);
				break;

			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
				stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x12);
				break;
			}
			break;
		}
	}

	switch (state->config->ts1_mode) {
	case STV090x_TSMODE_PARALLEL_PUNCTURED:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	case STV090x_TSMODE_DVBCI:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	case STV090x_TSMODE_SERIAL_PUNCTURED:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	case STV090x_TSMODE_SERIAL_CONTINUOUS:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	default:
		break;
	}

	switch (state->config->ts2_mode) {
	case STV090x_TSMODE_PARALLEL_PUNCTURED:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	case STV090x_TSMODE_DVBCI:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	case STV090x_TSMODE_SERIAL_PUNCTURED:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	case STV090x_TSMODE_SERIAL_CONTINUOUS:
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
		STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
		STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			goto err;
		break;

	default:
		break;
	}
	reg = stv090x_read_reg(state, STV090x_P2_TSCFGH);
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x01);
	if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
		goto err;
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x00);
	if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
		goto err;

	reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x01);
	if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
		goto err;
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x00);
	if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
		goto err;

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_init(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	const struct stv090x_config *config = state->config;
	u32 reg;

	stv090x_ldpc_mode(state, state->demod_mode);

	reg = STV090x_READ_DEMOD(state, TNRCFG2);
	STV090x_SETFIELD_Px(reg, TUN_IQSWAP_FIELD, state->inversion);
	if (STV090x_WRITE_DEMOD(state, TNRCFG2, reg) < 0)
		goto err;
	reg = STV090x_READ_DEMOD(state, DEMOD);
	STV090x_SETFIELD_Px(reg, ROLLOFF_CONTROL_FIELD, state->rolloff);
	if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
		goto err;

	stv090x_i2c_gate_ctrl(fe, 1);

	if (config->tuner_init)
		config->tuner_init(fe);

	stv090x_i2c_gate_ctrl(fe, 0);

	stv090x_set_tspath(state);

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static int stv090x_setup(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	const struct stv090x_config *config = state->config;
	const struct stv090x_reg *stv090x_initval = NULL;
	const struct stv090x_reg *stv090x_cut20_val = NULL;
	unsigned long t1_size = 0, t2_size = 0;
	u32 reg = 0;

	int i;

	if (state->device == STV0900) {
		dprintk(FE_DEBUG, 1, "Initializing STV0900");
		stv090x_initval = stv0900_initval;
		t1_size = ARRAY_SIZE(stv0900_initval);
		stv090x_cut20_val = stv0900_cut20_val;
		t2_size = ARRAY_SIZE(stv0900_cut20_val);
	} else if (state->device == STV0903) {
		dprintk(FE_DEBUG, 1, "Initializing STV0903");
		stv090x_initval = stv0903_initval;
		t1_size = ARRAY_SIZE(stv0903_initval);
		stv090x_cut20_val = stv0903_cut20_val;
		t2_size = ARRAY_SIZE(stv0903_cut20_val);
	}

	/* STV090x init */
	if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x5c) < 0) /* Stop Demod */
		goto err;

	msleep(5);

	if (STV090x_WRITE_DEMOD(state, TNRCFG, 0x6c) < 0) /* check register ! (No Tuner Mode) */
		goto err;

	STV090x_SETFIELD_Px(reg, ENARPT_LEVEL_FIELD, config->repeater_level);
	if (STV090x_WRITE_DEMOD(state, I2CRPT, reg) < 0) /* repeater OFF */
		goto err;

	if (stv090x_write_reg(state, STV090x_NCOARSE, 0x13) < 0) /* set PLL divider */
		goto err;
	msleep(5);
	if (stv090x_write_reg(state, STV090x_I2CCFG, 0x08) < 0) /* 1/41 oversampling */
		goto err;
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, 0x20 | config->clk_mode) < 0) /* enable PLL */
		goto err;
	msleep(5);

	/* write initval */
	for (i = 0; i < t1_size; i++) {
		dprintk(FE_DEBUG, 1, "Setting up initial values");
		if (stv090x_write_reg(state, stv090x_initval[i].addr, stv090x_initval[i].data) < 0)
			goto err;
	}

	if (state->dev_ver >= 0x20) {
		if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x0c) < 0)
			goto err;

		/* write cut20_val*/
		dprintk(FE_DEBUG, 1, "Setting up Cut 2.0 initial values");
		for (i = 0; i < t2_size; i++) {
			if (stv090x_write_reg(state, stv090x_cut20_val[i].addr, stv090x_cut20_val[i].data) < 0)
				goto err;
		}
	}

	if (stv090x_write_reg(state, STV090x_TSTRES0, 0x80) < 0)
		goto err;
	if (stv090x_write_reg(state, STV090x_TSTRES0, 0x00) < 0)
		goto err;

	stv090x_set_mclk(state, 135000000, config->xtal); /* 135 Mhz */
	msleep(5);
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, 0x20 | config->clk_mode) < 0)
		goto err;
	stv090x_get_mclk(state);

	return 0;
err:
	dprintk(FE_ERROR, 1, "I/O error");
	return -1;
}

static struct dvb_frontend_ops stv090x_ops = {

	.info = {
		.name			= "STV090x Multistandard",
	},

	.release			= stv090x_release,
	.init				= stv090x_init,

	.sleep				= stv090x_sleep,
	.get_frontend_algo		= stv090x_frontend_algo,

	.i2c_gate_ctrl			= stv090x_i2c_gate_ctrl,

	.diseqc_send_master_cmd		= stv090x_send_diseqc_msg,
	.diseqc_recv_slave_reply	= stv090x_recv_slave_reply,
	.set_tone			= stv090x_set_tone,

	.search				= stv090x_search,
	.read_status			= stv090x_read_status,
	.read_ber			= stv090x_read_per,
	.read_signal_strength		= stv090x_read_signal_strength,
	.read_snr			= stv090x_read_cnr
};


struct dvb_frontend *stv090x_attach(const struct stv090x_config *config,
				    struct i2c_adapter *i2c,
				    enum stv090x_demodulator demod)
{
	struct stv090x_state *state = NULL;

	state = kzalloc(sizeof (struct stv090x_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	state->verbose				= &verbose;
	state->config				= config;
	state->i2c				= i2c;
	state->frontend.ops			= stv090x_ops;
	state->frontend.demodulator_priv	= state;
	state->demod				= demod;
	state->demod_mode 			= config->demod_mode; /* Single or Dual mode */
	state->device				= config->device;
	state->rolloff				= 35; /* default */

	if (state->demod == STV090x_DEMODULATOR_0)
		mutex_init(&demod_lock);

	if (stv090x_sleep(&state->frontend) < 0) {
		dprintk(FE_ERROR, 1, "Error putting device to sleep");
		goto error;
	}

	if (stv090x_setup(&state->frontend) < 0) {
		dprintk(FE_ERROR, 1, "Error setting up device");
		goto error;
	}
	if (stv090x_wakeup(&state->frontend) < 0) {
		dprintk(FE_ERROR, 1, "Error waking device");
		goto error;
	}
	state->dev_ver = stv090x_read_reg(state, STV090x_MID);

	dprintk(FE_ERROR, 1, "Attaching %s demodulator(%d) Cut=0x%02x\n",
	       state->device == STV0900 ? "STV0900" : "STV0903",
	       demod,
	       state->dev_ver);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(stv090x_attach);
MODULE_PARM_DESC(verbose, "Set Verbosity level");
MODULE_AUTHOR("Manu Abraham");
MODULE_DESCRIPTION("STV090x Multi-Std Broadcast frontend");
MODULE_LICENSE("GPL");
