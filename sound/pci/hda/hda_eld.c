/*
 * Generic routines and proc interface for ELD(EDID Like Data) information
 *
 * Copyright(c) 2008 Intel Corporation.
 *
 * Authors:
 * 		Wu Fengguang <wfg@linux.intel.com>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <sound/core.h>
#include <asm/unaligned.h>
#include "hda_codec.h"
#include "hda_local.h"

enum eld_versions {
	ELD_VER_CEA_861D	= 2,
	ELD_VER_PARTIAL		= 31,
};

static char *eld_versoin_names[32] = {
	"reserved",
	"reserved",
	"CEA-861D or below",
	[3 ... 30] = "reserved",
	[31] = "partial"
};

enum cea_edid_versions {
	CEA_EDID_VER_NONE	= 0,
	CEA_EDID_VER_CEA861	= 1,
	CEA_EDID_VER_CEA861A	= 2,
	CEA_EDID_VER_CEA861BCD	= 3,
	CEA_EDID_VER_RESERVED	= 4,
};

static char *cea_edid_version_names[8] = {
	"no CEA EDID Timing Extension block present",
	"CEA-861",
	"CEA-861-A",
	"CEA-861-B, C or D",
	[4 ... 7] = "reserved"
};

static char *cea_speaker_allocation_names[] = {
	/*  0 */ "FL/FR",
	/*  1 */ "LFE",
	/*  2 */ "FC",
	/*  3 */ "RL/RR",
	/*  4 */ "RC",
	/*  5 */ "FLC/FRC",
	/*  6 */ "RLC/RRC",
	/*  7 */ "FLW/FRW",
	/*  8 */ "FLH/FRH",
	/*  9 */ "TC",
	/* 10 */ "FCH",
};

static char *eld_connection_type_names[4] = {
	"HDMI",
	"Display Port",
	"2-reserved",
	"3-reserved"
};

enum cea_audio_coding_types {
	AUDIO_CODING_TYPE_REF_STREAM_HEADER	=  0,
	AUDIO_CODING_TYPE_LPCM			=  1,
	AUDIO_CODING_TYPE_AC3			=  2,
	AUDIO_CODING_TYPE_MPEG1			=  3,
	AUDIO_CODING_TYPE_MP3			=  4,
	AUDIO_CODING_TYPE_MPEG2			=  5,
	AUDIO_CODING_TYPE_AACLC			=  6,
	AUDIO_CODING_TYPE_DTS			=  7,
	AUDIO_CODING_TYPE_ATRAC			=  8,
	AUDIO_CODING_TYPE_SACD			=  9,
	AUDIO_CODING_TYPE_EAC3			= 10,
	AUDIO_CODING_TYPE_DTS_HD		= 11,
	AUDIO_CODING_TYPE_MLP			= 12,
	AUDIO_CODING_TYPE_DST			= 13,
	AUDIO_CODING_TYPE_WMAPRO		= 14,
	AUDIO_CODING_TYPE_REF_CXT		= 15,
	/* also include valid xtypes below */
	AUDIO_CODING_TYPE_HE_AAC		= 15,
	AUDIO_CODING_TYPE_HE_AAC2		= 16,
	AUDIO_CODING_TYPE_MPEG_SURROUND		= 17,
};

enum cea_audio_coding_xtypes {
	AUDIO_CODING_XTYPE_HE_REF_CT		= 0,
	AUDIO_CODING_XTYPE_HE_AAC		= 1,
	AUDIO_CODING_XTYPE_HE_AAC2		= 2,
	AUDIO_CODING_XTYPE_MPEG_SURROUND	= 3,
	AUDIO_CODING_XTYPE_FIRST_RESERVED	= 4,
};

static char *cea_audio_coding_type_names[] = {
	/*  0 */ "undefined",
	/*  1 */ "LPCM",
	/*  2 */ "AC-3",
	/*  3 */ "MPEG1",
	/*  4 */ "MP3",
	/*  5 */ "MPEG2",
	/*  6 */ "AAC-LC",
	/*  7 */ "DTS",
	/*  8 */ "ATRAC",
	/*  9 */ "DSD (One Bit Audio)",
	/* 10 */ "E-AC-3/DD+ (Dolby Digital Plus)",
	/* 11 */ "DTS-HD",
	/* 12 */ "MLP (Dolby TrueHD)",
	/* 13 */ "DST",
	/* 14 */ "WMAPro",
	/* 15 */ "HE-AAC",
	/* 16 */ "HE-AACv2",
	/* 17 */ "MPEG Surround",
};

/*
 * The following two lists are shared between
 * 	- HDMI audio InfoFrame (source to sink)
 * 	- CEA E-EDID Extension (sink to source)
 */

/*
 * SS1:SS0 index => sample size
 */
static int cea_sample_sizes[4] = {
	0,	 		/* 0: Refer to Stream Header */
	AC_SUPPCM_BITS_16,	/* 1: 16 bits */
	AC_SUPPCM_BITS_20,	/* 2: 20 bits */
	AC_SUPPCM_BITS_24,	/* 3: 24 bits */
};

/*
 * SF2:SF1:SF0 index => sampling frequency
 */
static int cea_sampling_frequencies[8] = {
	0,			/* 0: Refer to Stream Header */
	SNDRV_PCM_RATE_32000,	/* 1:  32000Hz */
	SNDRV_PCM_RATE_44100,	/* 2:  44100Hz */
	SNDRV_PCM_RATE_48000,	/* 3:  48000Hz */
	SNDRV_PCM_RATE_88200,	/* 4:  88200Hz */
	SNDRV_PCM_RATE_96000,	/* 5:  96000Hz */
	SNDRV_PCM_RATE_176400,	/* 6: 176400Hz */
	SNDRV_PCM_RATE_192000,	/* 7: 192000Hz */
};

static unsigned char hdmi_get_eld_byte(struct hda_codec *codec, hda_nid_t nid,
					int byte_index)
{
	unsigned int val;

	val = snd_hda_codec_read(codec, nid, 0,
					AC_VERB_GET_HDMI_ELDD, byte_index);

#ifdef BE_PARANOID
	printk(KERN_INFO "ELD data byte %d: 0x%x\n", byte_index, val);
#endif

	if ((val & AC_ELDD_ELD_VALID) == 0) {
		snd_printd(KERN_INFO "Invalid ELD data byte %d\n",
								byte_index);
		val = 0;
	}

	return val & AC_ELDD_ELD_DATA;
}

#define GRAB_BITS(buf, byte, lowbit, bits) 		\
({							\
	BUILD_BUG_ON(lowbit > 7);			\
	BUILD_BUG_ON(bits > 8);				\
	BUILD_BUG_ON(bits <= 0);			\
							\
	(buf[byte] >> (lowbit)) & ((1 << (bits)) - 1);	\
})

static void hdmi_update_short_audio_desc(struct cea_sad *a,
					 const unsigned char *buf)
{
	int i;
	int val;

	val = GRAB_BITS(buf, 1, 0, 7);
	a->rates = 0;
	for (i = 0; i < 7; i++)
		if (val & (1 << i))
			a->rates |= cea_sampling_frequencies[i + 1];

	a->channels = GRAB_BITS(buf, 0, 0, 3);
	a->channels++;

	a->format = GRAB_BITS(buf, 0, 3, 4);
	switch (a->format) {
	case AUDIO_CODING_TYPE_REF_STREAM_HEADER:
		snd_printd(KERN_INFO
				"audio coding type 0 not expected in ELD\n");
		break;

	case AUDIO_CODING_TYPE_LPCM:
		val = GRAB_BITS(buf, 2, 0, 3);
		a->sample_bits = 0;
		for (i = 0; i < 3; i++)
			if (val & (1 << i))
				a->sample_bits |= cea_sample_sizes[i + 1];
		break;

	case AUDIO_CODING_TYPE_AC3:
	case AUDIO_CODING_TYPE_MPEG1:
	case AUDIO_CODING_TYPE_MP3:
	case AUDIO_CODING_TYPE_MPEG2:
	case AUDIO_CODING_TYPE_AACLC:
	case AUDIO_CODING_TYPE_DTS:
	case AUDIO_CODING_TYPE_ATRAC:
		a->max_bitrate = GRAB_BITS(buf, 2, 0, 8);
		a->max_bitrate *= 8000;
		break;

	case AUDIO_CODING_TYPE_SACD:
		break;

	case AUDIO_CODING_TYPE_EAC3:
		break;

	case AUDIO_CODING_TYPE_DTS_HD:
		break;

	case AUDIO_CODING_TYPE_MLP:
		break;

	case AUDIO_CODING_TYPE_DST:
		break;

	case AUDIO_CODING_TYPE_WMAPRO:
		a->profile = GRAB_BITS(buf, 2, 0, 3);
		break;

	case AUDIO_CODING_TYPE_REF_CXT:
		a->format = GRAB_BITS(buf, 2, 3, 5);
		if (a->format == AUDIO_CODING_XTYPE_HE_REF_CT ||
		    a->format >= AUDIO_CODING_XTYPE_FIRST_RESERVED) {
			snd_printd(KERN_INFO
				"audio coding xtype %d not expected in ELD\n",
				a->format);
			a->format = 0;
		} else
			a->format += AUDIO_CODING_TYPE_HE_AAC -
				     AUDIO_CODING_XTYPE_HE_AAC;
		break;
	}
}

/*
 * Be careful, ELD buf could be totally rubbish!
 */
static int hdmi_update_eld(struct hdmi_eld *e,
			   const unsigned char *buf, int size)
{
	int mnl;
	int i;

	e->eld_ver = GRAB_BITS(buf, 0, 3, 5);
	if (e->eld_ver != ELD_VER_CEA_861D &&
	    e->eld_ver != ELD_VER_PARTIAL) {
		snd_printd(KERN_INFO "Unknown ELD version %d\n", e->eld_ver);
		goto out_fail;
	}

	e->eld_size = size;
	e->baseline_len = GRAB_BITS(buf, 2, 0, 8);
	mnl		= GRAB_BITS(buf, 4, 0, 5);
	e->cea_edid_ver	= GRAB_BITS(buf, 4, 5, 3);

	e->support_hdcp	= GRAB_BITS(buf, 5, 0, 1);
	e->support_ai	= GRAB_BITS(buf, 5, 1, 1);
	e->conn_type	= GRAB_BITS(buf, 5, 2, 2);
	e->sad_count	= GRAB_BITS(buf, 5, 4, 4);

	e->aud_synch_delay = GRAB_BITS(buf, 6, 0, 8) * 2;
	e->spk_alloc	= GRAB_BITS(buf, 7, 0, 7);

	e->port_id	  = get_unaligned_le64(buf + 8);

	/* not specified, but the spec's tendency is little endian */
	e->manufacture_id = get_unaligned_le16(buf + 16);
	e->product_id	  = get_unaligned_le16(buf + 18);

	if (mnl > ELD_MAX_MNL) {
		snd_printd(KERN_INFO "MNL is reserved value %d\n", mnl);
		goto out_fail;
	} else if (ELD_FIXED_BYTES + mnl > size) {
		snd_printd(KERN_INFO "out of range MNL %d\n", mnl);
		goto out_fail;
	} else
		strlcpy(e->monitor_name, buf + ELD_FIXED_BYTES, mnl);

	for (i = 0; i < e->sad_count; i++) {
		if (ELD_FIXED_BYTES + mnl + 3 * (i + 1) > size) {
			snd_printd(KERN_INFO "out of range SAD %d\n", i);
			goto out_fail;
		}
		hdmi_update_short_audio_desc(e->sad + i,
					buf + ELD_FIXED_BYTES + mnl + 3 * i);
	}

	return 0;

out_fail:
	e->eld_ver = 0;
	return -EINVAL;
}

static int hdmi_present_sense(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_PIN_SENSE, 0);
}

static int hdmi_eld_valid(struct hda_codec *codec, hda_nid_t nid)
{
	int eldv;
	int present;

	present = hdmi_present_sense(codec, nid);
	eldv    = (present & AC_PINSENSE_ELDV);
	present = (present & AC_PINSENSE_PRESENCE);

#ifdef CONFIG_SND_DEBUG_VERBOSE
	printk(KERN_INFO "pinp = %d, eldv = %d\n", !!present, !!eldv);
#endif

	return eldv && present;
}

int snd_hdmi_get_eld_size(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_HDMI_DIP_SIZE,
						 AC_DIPSIZE_ELD_BUF);
}

int snd_hdmi_get_eld(struct hdmi_eld *eld,
		     struct hda_codec *codec, hda_nid_t nid)
{
	int i;
	int ret;
	int size;
	unsigned char *buf;

	if (!hdmi_eld_valid(codec, nid))
		return -ENOENT;

	size = snd_hdmi_get_eld_size(codec, nid);
	if (size == 0) {
		/* wfg: workaround for ASUS P5E-VM HDMI board */
		snd_printd(KERN_INFO "ELD buf size is 0, force 128\n");
		size = 128;
	}
	if (size < ELD_FIXED_BYTES || size > PAGE_SIZE) {
		snd_printd(KERN_INFO "Invalid ELD buf size %d\n", size);
		return -ERANGE;
	}

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < size; i++)
		buf[i] = hdmi_get_eld_byte(codec, nid, i);

	ret = hdmi_update_eld(eld, buf, size);

	kfree(buf);
	return ret;
}

static void hdmi_show_short_audio_desc(struct cea_sad *a)
{
	char buf[SND_PRINT_RATES_ADVISED_BUFSIZE];

	printk(KERN_INFO "coding type: %s\n",
					cea_audio_coding_type_names[a->format]);
	printk(KERN_INFO "channels: %d\n", a->channels);

	snd_print_pcm_rates(a->rates, buf, sizeof(buf));
	printk(KERN_INFO "sampling rates: %s\n", buf);

	if (a->format == AUDIO_CODING_TYPE_LPCM)
		printk(KERN_INFO "sample bits: 0x%x\n", a->sample_bits);

	if (a->max_bitrate)
		printk(KERN_INFO "max bitrate: %d\n", a->max_bitrate);

	if (a->profile)
		printk(KERN_INFO "profile: %d\n", a->profile);
}

void snd_print_channel_allocation(int spk_alloc, char *buf, int buflen)
{
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(cea_speaker_allocation_names); i++) {
		if (spk_alloc & (1 << i))
			j += snprintf(buf + j, buflen - j,  " %s",
					cea_speaker_allocation_names[i]);
	}
	buf[j] = '\0';	/* necessary when j == 0 */
}

void snd_hdmi_show_eld(struct hdmi_eld *e)
{
	int i;
	char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];

	printk(KERN_INFO "ELD buffer size  is %d\n", e->eld_size);
	printk(KERN_INFO "ELD baseline len is %d*4\n", e->baseline_len);
	printk(KERN_INFO "vendor block len is %d\n",
					e->eld_size - e->baseline_len * 4 - 4);
	printk(KERN_INFO "ELD version      is %s\n",
					eld_versoin_names[e->eld_ver]);
	printk(KERN_INFO "CEA EDID version is %s\n",
				cea_edid_version_names[e->cea_edid_ver]);
	printk(KERN_INFO "manufacture id   is 0x%x\n", e->manufacture_id);
	printk(KERN_INFO "product id       is 0x%x\n", e->product_id);
	printk(KERN_INFO "port id          is 0x%llx\n", (long long)e->port_id);
	printk(KERN_INFO "HDCP support     is %d\n", e->support_hdcp);
	printk(KERN_INFO "AI support       is %d\n", e->support_ai);
	printk(KERN_INFO "SAD count        is %d\n", e->sad_count);
	printk(KERN_INFO "audio sync delay is %x\n", e->aud_synch_delay);
	printk(KERN_INFO "connection type  is %s\n",
				eld_connection_type_names[e->conn_type]);
	printk(KERN_INFO "monitor name     is %s\n", e->monitor_name);

	snd_print_channel_allocation(e->spk_alloc, buf, sizeof(buf));
	printk(KERN_INFO "speaker allocations: (0x%x)%s\n", e->spk_alloc, buf);

	for (i = 0; i < e->sad_count; i++)
		hdmi_show_short_audio_desc(e->sad + i);
}

#ifdef CONFIG_PROC_FS

static void hdmi_print_sad_info(int i, struct cea_sad *a,
				struct snd_info_buffer *buffer)
{
	char buf[SND_PRINT_RATES_ADVISED_BUFSIZE];

	snd_iprintf(buffer, "sad%d_coding_type\t[0x%x] %s\n",
			i, a->format, cea_audio_coding_type_names[a->format]);
	snd_iprintf(buffer, "sad%d_channels\t\t%d\n", i, a->channels);

	snd_print_pcm_rates(a->rates, buf, sizeof(buf));
	snd_iprintf(buffer, "sad%d_rates\t\t[0x%x]%s\n", i, a->rates, buf);

	if (a->format == AUDIO_CODING_TYPE_LPCM) {
		snd_print_pcm_bits(a->sample_bits, buf, sizeof(buf));
		snd_iprintf(buffer, "sad%d_bits\t\t[0x%x]%s\n",
							i, a->sample_bits, buf);
	}

	if (a->max_bitrate)
		snd_iprintf(buffer, "sad%d_max_bitrate\t%d\n",
							i, a->max_bitrate);

	if (a->profile)
		snd_iprintf(buffer, "sad%d_profile\t\t%d\n", i, a->profile);
}

static void hdmi_print_eld_info(struct snd_info_entry *entry,
				struct snd_info_buffer *buffer)
{
	struct hdmi_eld *e = entry->private_data;
	char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];
	int i;

	snd_iprintf(buffer, "monitor name\t\t%s\n", e->monitor_name);
	snd_iprintf(buffer, "connection_type\t\t%s\n",
				eld_connection_type_names[e->conn_type]);
	snd_iprintf(buffer, "eld_version\t\t[0x%x] %s\n", e->eld_ver,
					eld_versoin_names[e->eld_ver]);
	snd_iprintf(buffer, "edid_version\t\t[0x%x] %s\n", e->cea_edid_ver,
				cea_edid_version_names[e->cea_edid_ver]);
	snd_iprintf(buffer, "manufacture_id\t\t0x%x\n", e->manufacture_id);
	snd_iprintf(buffer, "product_id\t\t0x%x\n", e->product_id);
	snd_iprintf(buffer, "port_id\t\t\t0x%llx\n", (long long)e->port_id);
	snd_iprintf(buffer, "support_hdcp\t\t%d\n", e->support_hdcp);
	snd_iprintf(buffer, "support_ai\t\t%d\n", e->support_ai);
	snd_iprintf(buffer, "audio_sync_delay\t%d\n", e->aud_synch_delay);

	snd_print_channel_allocation(e->spk_alloc, buf, sizeof(buf));
	snd_iprintf(buffer, "speakers\t\t[0x%x]%s\n", e->spk_alloc, buf);

	snd_iprintf(buffer, "sad_count\t\t%d\n", e->sad_count);

	for (i = 0; i < e->sad_count; i++)
		hdmi_print_sad_info(i, e->sad + i, buffer);
}

int snd_hda_eld_proc_new(struct hda_codec *codec, struct hdmi_eld *eld)
{
	char name[32];
	struct snd_info_entry *entry;
	int err;

	snprintf(name, sizeof(name), "eld#%d", codec->addr);
	err = snd_card_proc_new(codec->bus->card, name, &entry);
	if (err < 0)
		return err;

	snd_info_set_text_ops(entry, eld, hdmi_print_eld_info);
	return 0;
}

#endif
