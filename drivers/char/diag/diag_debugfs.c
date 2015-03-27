/* Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/slab.h>
#include <linux/debugfs.h>
#include "diagchar.h"
#include "diagfwd.h"
#ifdef CONFIG_DIAGFWD_BRIDGE_CODE
#include "diagfwd_bridge.h"
#include "diagfwd_hsic.h"
#include "diagfwd_smux.h"
#include "diagfwd_mhi.h"
#endif
#include "diagmem.h"
#include "diag_dci.h"
#include "diag_usb.h"
#include "diag_debugfs.h"

#define DEBUG_BUF_SIZE	4096
static struct dentry *diag_dbgfs_dent;
static int diag_dbgfs_table_index;
static int diag_dbgfs_mempool_index;
static int diag_dbgfs_usbinfo_index;
static int diag_dbgfs_hsicinfo_index;
static int diag_dbgfs_mhiinfo_index;
static int diag_dbgfs_bridgeinfo_index;
static int diag_dbgfs_finished;
static int diag_dbgfs_dci_data_index;
static int diag_dbgfs_dci_finished;

static ssize_t diag_dbgfs_read_status(struct file *file, char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	char *buf;
	int ret, i;
	unsigned int buf_size;
	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (!buf) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}
	buf_size = ksize(buf);
	ret = scnprintf(buf, buf_size,
		"modem ch: 0x%p\n"
		"lpass ch: 0x%p\n"
		"riva ch: 0x%p\n"
		"sensors ch: 0x%p\n"
		"modem dci ch: 0x%p\n"
		"lpass dci ch: 0x%p\n"
		"riva dci ch: 0x%p\n"
		"sensors dci ch: 0x%p\n"
		"modem cntl_ch: 0x%p\n"
		"lpass cntl_ch: 0x%p\n"
		"riva cntl_ch: 0x%p\n"
		"sensors cntl_ch: 0x%p\n"
		"modem cmd ch: 0x%p\n"
		"adsp cmd ch: 0x%p\n"
		"riva cmd ch: 0x%p\n"
		"sensors cmd ch: 0x%p\n"
		"modem dci cmd ch: 0x%p\n"
		"lpass dci cmd ch: 0x%p\n"
		"riva dci cmd ch: 0x%p\n"
		"sensors dci cmd ch: 0x%p\n"
		"CPU Tools id: %d\n"
		"Apps only: %d\n"
		"Apps master: %d\n"
		"Check Polling Response: %d\n"
		"polling_reg_flag: %d\n"
		"uses device tree: %d\n"
		"supports separate cmdrsp: %d\n"
		"Modem separate cmdrsp: %d\n"
		"LPASS separate cmdrsp: %d\n"
		"RIVA separate cmdrsp: %d\n"
		"SENSORS separate cmdrsp: %d\n"
		"Modem in_busy_1: %d\n"
		"Modem in_busy_2: %d\n"
		"LPASS in_busy_1: %d\n"
		"LPASS in_busy_2: %d\n"
		"RIVA in_busy_1: %d\n"
		"RIVA in_busy_2: %d\n"
		"SENSORS in_busy_1: %d\n"
		"SENSORS in_busy_2: %d\n"
		"DCI Modem in_busy_1: %d\n"
		"DCI LPASS in_busy_1: %d\n"
		"DCI WCNSS in_busy_1: %d\n"
		"DCI SENSORS in_busy_1: %d\n"
		"Modem CMD in_busy_1: %d\n"
		"Modem CMD in_busy_2: %d\n"
		"DCI CMD Modem in_busy_1: %d\n"
		"DCI CMD LPASS in_busy_1: %d\n"
		"DCI CMD WCNSS in_busy_1: %d\n"
		"DCI CMD SENSORS in_busy_1: %d\n"
		"ADSP CMD in_busy_1: %d\n"
		"ADSP CMD in_busy_2: %d\n"
		"RIVA CMD in_busy_1: %d\n"
		"RIVA CMD in_busy_2: %d\n"
		"SENSORS CMD in_busy_1: %d\n"
		"SENSORS CMD in_busy_2: %d\n"
		"Modem supports STM: %d\n"
		"LPASS supports STM: %d\n"
		"RIVA supports STM: %d\n"
		"SENSORS supports STM: %d\n"
		"Modem STM state: %d\n"
		"LPASS STM state: %d\n"
		"RIVA STM state: %d\n"
		"SENSORS STM state: %d\n"
		"APPS STM state: %d\n"
		"Modem STM requested state: %d\n"
		"LPASS STM requested state: %d\n"
		"RIVA STM requested state: %d\n"
		"SENSORS STM requested state: %d\n"
		"APPS STM requested state: %d\n"
		"supports apps hdlc encoding: %d\n"
		"Modem hdlc encoding: %d\n"
		"Lpass hdlc encoding: %d\n"
		"RIVA hdlc encoding: %d\n"
		"SENSORS hdlc encoding: %d\n"
		"Modem DATA in_buf_1_size: %d\n"
		"Modem DATA in_buf_2_size: %d\n"
		"ADSP DATA in_buf_1_size: %d\n"
		"ADSP DATA in_buf_2_size: %d\n"
		"RIVA DATA in_buf_1_size: %d\n"
		"RIVA DATA in_buf_2_size: %d\n"
		"SENSORS DATA in_buf_1_size: %d\n"
		"SENSORS DATA in_buf_2_size: %d\n"
		"Modem DATA in_buf_1_raw_size: %d\n"
		"Modem DATA in_buf_2_raw_size: %d\n"
		"ADSP DATA in_buf_1_raw_size: %d\n"
		"ADSP DATA in_buf_2_raw_size: %d\n"
		"RIVA DATA in_buf_1_raw_size: %d\n"
		"RIVA DATA in_buf_2_raw_size: %d\n"
		"SENSORS DATA in_buf_1_raw_size: %d\n"
		"SENSORS DATA in_buf_2_raw_size: %d\n"
		"Modem CMD in_buf_1_size: %d\n"
		"Modem CMD in_buf_1_raw_size: %d\n"
		"ADSP CMD in_buf_1_size: %d\n"
		"ADSP CMD in_buf_1_raw_size: %d\n"
		"RIVA CMD in_buf_1_size: %d\n"
		"RIVA CMD in_buf_1_raw_size: %d\n"
		"SENSORS CMD in_buf_1_size: %d\n"
		"SENSORS CMD in_buf_1_raw_size: %d\n"
		"Modem CNTL in_buf_1_size: %d\n"
		"ADSP CNTL in_buf_1_size: %d\n"
		"RIVA CNTL in_buf_1_size: %d\n"
		"SENSORS CNTL in_buf_1_size: %d\n"
		"Modem DCI in_buf_1_size: %d\n"
		"Modem DCI CMD in_buf_1_size: %d\n"
		"LPASS DCI in_buf_1_size: %d\n"
		"LPASS DCI CMD in_buf_1_size: %d\n"
		"WCNSS DCI in_buf_1_size: %d\n"
		"WCNSS DCI CMD in_buf_1_size: %d\n"
		"SENSORS DCI in_buf_1_size: %d\n"
		"SENSORS DCI CMD in_buf_1_size: %d\n"
		"Received Feature mask from Modem: %d\n"
		"Received Feature mask from LPASS: %d\n"
		"Received Feature mask from WCNSS: %d\n"
		"Received Feature mask from SENSORS: %d\n"
		"Mask Centralization Support on Modem: %d\n"
		"Mask Centralization Support on LPASS: %d\n"
		"Mask Centralization Support on WCNSS: %d\n"
		"Mask Centralization Support on SENSORS: %d\n"
		"Buffering Mode Support on Modem: %d\n"
		"Buffering Mode Support on LPASS: %d\n"
		"Buffering Mode Support on WCNSS: %d\n"
		"Buffering Mode Support on SENSORS: %d\n"
		"Buffering Mode on Modem: %d\n"
		"Buffering Mode on LPASS: %d\n"
		"Buffering Mode on WCNSS: %d\n"
		"Buffering Mode on SENSORS: %d\n"
		"logging_mode: %d\n"
		"rsp_in_busy: %d\n"
		"hdlc disabled: %d\n",
		driver->smd_data[PERIPHERAL_MODEM].ch,
		driver->smd_data[PERIPHERAL_LPASS].ch,
		driver->smd_data[PERIPHERAL_WCNSS].ch,
		driver->smd_data[PERIPHERAL_SENSORS].ch,
		driver->smd_dci[PERIPHERAL_MODEM].ch,
		driver->smd_dci[PERIPHERAL_LPASS].ch,
		driver->smd_dci[PERIPHERAL_WCNSS].ch,
		driver->smd_dci[PERIPHERAL_SENSORS].ch,
		driver->smd_cntl[PERIPHERAL_MODEM].ch,
		driver->smd_cntl[PERIPHERAL_LPASS].ch,
		driver->smd_cntl[PERIPHERAL_WCNSS].ch,
		driver->smd_cntl[PERIPHERAL_SENSORS].ch,
		driver->smd_cmd[PERIPHERAL_MODEM].ch,
		driver->smd_cmd[PERIPHERAL_LPASS].ch,
		driver->smd_cmd[PERIPHERAL_WCNSS].ch,
		driver->smd_cmd[PERIPHERAL_SENSORS].ch,
		driver->smd_dci_cmd[PERIPHERAL_MODEM].ch,
		driver->smd_dci_cmd[PERIPHERAL_LPASS].ch,
		driver->smd_dci_cmd[PERIPHERAL_WCNSS].ch,
		driver->smd_dci_cmd[PERIPHERAL_SENSORS].ch,
		chk_config_get_id(),
		chk_apps_only(),
		chk_apps_master(),
		chk_polling_response(),
		driver->polling_reg_flag,
		driver->use_device_tree,
		driver->supports_separate_cmdrsp,
		driver->feature[PERIPHERAL_MODEM].separate_cmd_rsp,
		driver->feature[PERIPHERAL_LPASS].separate_cmd_rsp,
		driver->feature[PERIPHERAL_WCNSS].separate_cmd_rsp,
		driver->feature[PERIPHERAL_SENSORS].separate_cmd_rsp,
		driver->smd_data[PERIPHERAL_MODEM].in_busy_1,
		driver->smd_data[PERIPHERAL_MODEM].in_busy_2,
		driver->smd_data[PERIPHERAL_LPASS].in_busy_1,
		driver->smd_data[PERIPHERAL_LPASS].in_busy_2,
		driver->smd_data[PERIPHERAL_WCNSS].in_busy_1,
		driver->smd_data[PERIPHERAL_WCNSS].in_busy_2,
		driver->smd_data[PERIPHERAL_SENSORS].in_busy_1,
		driver->smd_data[PERIPHERAL_SENSORS].in_busy_2,
		driver->smd_dci[PERIPHERAL_MODEM].in_busy_1,
		driver->smd_dci[PERIPHERAL_LPASS].in_busy_1,
		driver->smd_dci[PERIPHERAL_WCNSS].in_busy_1,
		driver->smd_dci[PERIPHERAL_SENSORS].in_busy_1,
		driver->smd_cmd[PERIPHERAL_MODEM].in_busy_1,
		driver->smd_cmd[PERIPHERAL_MODEM].in_busy_2,
		driver->smd_cmd[PERIPHERAL_LPASS].in_busy_1,
		driver->smd_cmd[PERIPHERAL_LPASS].in_busy_2,
		driver->smd_cmd[PERIPHERAL_WCNSS].in_busy_1,
		driver->smd_cmd[PERIPHERAL_WCNSS].in_busy_2,
		driver->smd_cmd[PERIPHERAL_SENSORS].in_busy_1,
		driver->smd_cmd[PERIPHERAL_SENSORS].in_busy_2,
		driver->smd_dci_cmd[PERIPHERAL_MODEM].in_busy_1,
		driver->smd_dci_cmd[PERIPHERAL_LPASS].in_busy_1,
		driver->smd_dci_cmd[PERIPHERAL_WCNSS].in_busy_1,
		driver->smd_dci_cmd[PERIPHERAL_SENSORS].in_busy_1,
		driver->feature[PERIPHERAL_MODEM].stm_support,
		driver->feature[PERIPHERAL_LPASS].stm_support,
		driver->feature[PERIPHERAL_WCNSS].stm_support,
		driver->feature[PERIPHERAL_SENSORS].stm_support,
		driver->stm_state[PERIPHERAL_MODEM],
		driver->stm_state[PERIPHERAL_LPASS],
		driver->stm_state[PERIPHERAL_WCNSS],
		driver->stm_state[PERIPHERAL_SENSORS],
		driver->stm_state[APPS_DATA],
		driver->stm_state_requested[PERIPHERAL_MODEM],
		driver->stm_state_requested[PERIPHERAL_LPASS],
		driver->stm_state_requested[PERIPHERAL_WCNSS],
		driver->stm_state_requested[PERIPHERAL_SENSORS],
		driver->stm_state_requested[APPS_DATA],
		driver->supports_apps_hdlc_encoding,
		driver->feature[PERIPHERAL_MODEM].encode_hdlc,
		driver->feature[PERIPHERAL_LPASS].encode_hdlc,
		driver->feature[PERIPHERAL_WCNSS].encode_hdlc,
		driver->feature[PERIPHERAL_SENSORS].encode_hdlc,
		(unsigned int)driver->smd_data[PERIPHERAL_MODEM].buf_in_1_size,
		(unsigned int)driver->smd_data[PERIPHERAL_MODEM].buf_in_2_size,
		(unsigned int)driver->smd_data[PERIPHERAL_LPASS].buf_in_1_size,
		(unsigned int)driver->smd_data[PERIPHERAL_LPASS].buf_in_2_size,
		(unsigned int)driver->smd_data[PERIPHERAL_WCNSS].buf_in_1_size,
		(unsigned int)driver->smd_data[PERIPHERAL_WCNSS].buf_in_2_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_SENSORS].buf_in_1_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_SENSORS].buf_in_2_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_MODEM].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_MODEM].buf_in_2_raw_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_LPASS].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_LPASS].buf_in_2_raw_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_WCNSS].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_WCNSS].buf_in_2_raw_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_SENSORS].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_data[PERIPHERAL_SENSORS].buf_in_2_raw_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_MODEM].buf_in_1_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_MODEM].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_LPASS].buf_in_1_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_LPASS].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_WCNSS].buf_in_1_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_WCNSS].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_SENSORS].buf_in_1_size,
		(unsigned int)
		driver->smd_cmd[PERIPHERAL_SENSORS].buf_in_1_raw_size,
		(unsigned int)
		driver->smd_cntl[PERIPHERAL_MODEM].buf_in_1_size,
		(unsigned int)
		driver->smd_cntl[PERIPHERAL_LPASS].buf_in_1_size,
		(unsigned int)
		driver->smd_cntl[PERIPHERAL_WCNSS].buf_in_1_size,
		(unsigned int)
		driver->smd_cntl[PERIPHERAL_SENSORS].buf_in_1_size,
		(unsigned int)
		driver->smd_dci[PERIPHERAL_MODEM].buf_in_1_size,
		(unsigned int)
		driver->smd_dci_cmd[PERIPHERAL_MODEM].buf_in_1_size,
		(unsigned int)
		driver->smd_dci[PERIPHERAL_LPASS].buf_in_1_size,
		(unsigned int)
		driver->smd_dci_cmd[PERIPHERAL_LPASS].buf_in_1_size,
		(unsigned int)
		driver->smd_dci[PERIPHERAL_WCNSS].buf_in_1_size,
		(unsigned int)
		driver->smd_dci_cmd[PERIPHERAL_WCNSS].buf_in_1_size,
		(unsigned int)
		driver->smd_dci[PERIPHERAL_SENSORS].buf_in_1_size,
		(unsigned int)
		driver->smd_dci_cmd[PERIPHERAL_SENSORS].buf_in_1_size,
		driver->feature[PERIPHERAL_MODEM].rcvd_feature_mask,
		driver->feature[PERIPHERAL_LPASS].rcvd_feature_mask,
		driver->feature[PERIPHERAL_WCNSS].rcvd_feature_mask,
		driver->feature[PERIPHERAL_SENSORS].rcvd_feature_mask,
		driver->feature[PERIPHERAL_MODEM].mask_centralization,
		driver->feature[PERIPHERAL_LPASS].mask_centralization,
		driver->feature[PERIPHERAL_WCNSS].mask_centralization,
		driver->feature[PERIPHERAL_SENSORS].mask_centralization,
		driver->feature[PERIPHERAL_MODEM].peripheral_buffering,
		driver->feature[PERIPHERAL_LPASS].peripheral_buffering,
		driver->feature[PERIPHERAL_WCNSS].peripheral_buffering,
		driver->feature[PERIPHERAL_SENSORS].peripheral_buffering,
		driver->buffering_mode[PERIPHERAL_MODEM].mode,
		driver->buffering_mode[PERIPHERAL_LPASS].mode,
		driver->buffering_mode[PERIPHERAL_WCNSS].mode,
		driver->buffering_mode[PERIPHERAL_SENSORS].mode,
		driver->logging_mode,
		driver->rsp_buf_busy,
		driver->hdlc_disabled);

#ifdef CONFIG_DIAG_OVER_USB
	ret += scnprintf(buf+ret, buf_size-ret,
		"usb_connected: %d\n",
		driver->usb_connected);
#endif

	for (i = 0; i < DIAG_NUM_PROC; i++) {
		ret += scnprintf(buf+ret, buf_size-ret,
				 "real_time proc: %d: %d\n", i,
				 driver->real_time_mode[i]);
	}

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, ret);

	kfree(buf);
	return ret;
}

static ssize_t diag_dbgfs_read_dcistats(struct file *file,
				char __user *ubuf, size_t count, loff_t *ppos)
{
	char *buf = NULL;
	unsigned int bytes_remaining, bytes_written = 0;
	unsigned int bytes_in_buf = 0, i = 0;
	struct diag_dci_data_info *temp_data = dci_traffic;
	unsigned int buf_size;
	buf_size = (DEBUG_BUF_SIZE < count) ? DEBUG_BUF_SIZE : count;

	if (diag_dbgfs_dci_finished) {
		diag_dbgfs_dci_finished = 0;
		return 0;
	}

	buf = kzalloc(sizeof(char) * buf_size, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	bytes_remaining = buf_size;

	if (diag_dbgfs_dci_data_index == 0) {
		bytes_written =
			scnprintf(buf, buf_size,
			"number of clients: %d\n"
			"dci proc active: %d\n"
			"dci real time vote: %d\n",
			driver->num_dci_client,
			(driver->proc_active_mask & DIAG_PROC_DCI) ? 1 : 0,
			(driver->proc_rt_vote_mask[DIAG_LOCAL_PROC] &
							DIAG_PROC_DCI) ? 1 : 0);
		bytes_in_buf += bytes_written;
		bytes_remaining -= bytes_written;
#ifdef CONFIG_DIAG_OVER_USB
		bytes_written = scnprintf(buf+bytes_in_buf, bytes_remaining,
			"usb_connected: %d\n",
			driver->usb_connected);
		bytes_in_buf += bytes_written;
		bytes_remaining -= bytes_written;
#endif
		bytes_written = scnprintf(buf+bytes_in_buf,
					  bytes_remaining,
					  "dci power: active, relax: %lu, %lu\n",
					  driver->diag_dev->power.wakeup->
						active_count,
					  driver->diag_dev->
						power.wakeup->relax_count);
		bytes_in_buf += bytes_written;
		bytes_remaining -= bytes_written;

	}
	temp_data += diag_dbgfs_dci_data_index;
	for (i = diag_dbgfs_dci_data_index; i < DIAG_DCI_DEBUG_CNT; i++) {
		if (temp_data->iteration != 0) {
			bytes_written = scnprintf(
				buf + bytes_in_buf, bytes_remaining,
				"i %-5ld\t"
				"s %-5d\t"
				"p %-5d\t"
				"r %-5d\t"
				"c %-5d\t"
				"t %-15s\n",
				temp_data->iteration,
				temp_data->data_size,
				temp_data->peripheral,
				temp_data->proc,
				temp_data->ch_type,
				temp_data->time_stamp);
			bytes_in_buf += bytes_written;
			bytes_remaining -= bytes_written;
			/* Check if there is room for another entry */
			if (bytes_remaining < bytes_written)
				break;
		}
		temp_data++;
	}

	diag_dbgfs_dci_data_index = (i >= DIAG_DCI_DEBUG_CNT) ? 0 : i + 1;
	bytes_written = simple_read_from_buffer(ubuf, count, ppos, buf,
								bytes_in_buf);
	kfree(buf);
	diag_dbgfs_dci_finished = 1;
	return bytes_written;
}

static ssize_t diag_dbgfs_read_power(struct file *file, char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	char *buf;
	int ret;
	unsigned int buf_size;

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (!buf) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	ret = scnprintf(buf, buf_size,
		"DCI reference count: %d\n"
		"DCI copy count: %d\n"
		"DCI Client Count: %d\n\n"
		"Memory Device reference count: %d\n"
		"Memory Device copy count: %d\n"
		"Logging mode: %d\n\n"
		"Wakeup source active count: %lu\n"
		"Wakeup source relax count: %lu\n\n",
		driver->dci_ws.ref_count,
		driver->dci_ws.copy_count,
		driver->num_dci_client,
		driver->md_ws.ref_count,
		driver->md_ws.copy_count,
		driver->logging_mode,
		driver->diag_dev->power.wakeup->active_count,
		driver->diag_dev->power.wakeup->relax_count);

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, ret);

	kfree(buf);
	return ret;
}

static ssize_t diag_dbgfs_read_workpending(struct file *file,
				char __user *ubuf, size_t count, loff_t *ppos)
{
	char *buf;
	int ret;
	unsigned int buf_size;

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (!buf) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	ret = scnprintf(buf, buf_size,
		"Pending status for work_stucts:\n"
		"diag_drain_work: %d\n"
		"Modem data diag_read_smd_work: %d\n"
		"LPASS data diag_read_smd_work: %d\n"
		"RIVA data diag_read_smd_work: %d\n"
		"SENSORS data diag_read_smd_work: %d\n"
		"Modem cntl diag_read_smd_work: %d\n"
		"LPASS cntl diag_read_smd_work: %d\n"
		"RIVA cntl diag_read_smd_work: %d\n"
		"SENSORS cntl diag_read_smd_work: %d\n"
		"Modem dci diag_read_smd_work: %d\n"
		"LPASS dci diag_read_smd_work: %d\n"
		"WCNSS dci diag_read_smd_work: %d\n"
		"SENSORS dci diag_read_smd_work: %d\n"
		"Modem data diag_notify_update_smd_work: %d\n"
		"LPASS data diag_notify_update_smd_work: %d\n"
		"RIVA data diag_notify_update_smd_work: %d\n"
		"SENSORS data diag_notify_update_smd_work: %d\n"
		"Modem cntl diag_notify_update_smd_work: %d\n"
		"LPASS cntl diag_notify_update_smd_work: %d\n"
		"RIVA cntl diag_notify_update_smd_work: %d\n"
		"SENSORS cntl diag_notify_update_smd_work: %d\n"
		"Modem dci diag_notify_update_smd_work: %d\n"
		"LPASS dci diag_notify_update_smd_work: %d\n"
		"WCNSS dci diag_notify_update_smd_work: %d\n"
		"SENSORS dci diag_notify_update_smd_work: %d\n",
		work_pending(&(driver->diag_drain_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_MODEM].
							diag_read_smd_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_LPASS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_WCNSS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_SENSORS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_MODEM].
							diag_read_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_LPASS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_WCNSS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_SENSORS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_MODEM].
							diag_read_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_LPASS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_WCNSS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_SENSORS].
							diag_read_smd_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_MODEM].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_LPASS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_WCNSS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_data[PERIPHERAL_SENSORS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_MODEM].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_LPASS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_WCNSS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_cntl[PERIPHERAL_SENSORS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_MODEM].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_LPASS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_WCNSS].
						diag_notify_update_smd_work)),
		work_pending(&(driver->smd_dci[PERIPHERAL_SENSORS].
						diag_notify_update_smd_work)));
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, ret);

	kfree(buf);
	return ret;
}

static ssize_t diag_dbgfs_read_table(struct file *file, char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	char *buf;
	int ret = 0;
	int i = 0;
	int is_polling = 0;
	unsigned int bytes_remaining;
	unsigned int bytes_in_buffer = 0;
	unsigned int bytes_written;
	unsigned int buf_size;
	struct list_head *start;
	struct list_head *temp;
	struct diag_cmd_reg_t *item = NULL;

	if (diag_dbgfs_table_index == driver->cmd_reg_count) {
		diag_dbgfs_table_index = 0;
		return 0;
	}

	buf_size = (DEBUG_BUF_SIZE < count) ? DEBUG_BUF_SIZE : count;

	buf = kzalloc(sizeof(char) * buf_size, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}
	buf_size = ksize(buf);
	bytes_remaining = buf_size;

	if (diag_dbgfs_table_index == 0) {
		bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
					  "Client ids: Modem: %d, LPASS: %d, WCNSS: %d, SLPI: %d, APPS: %d\n",
					  PERIPHERAL_MODEM, PERIPHERAL_LPASS,
					  PERIPHERAL_WCNSS, PERIPHERAL_SENSORS,
					  APPS_DATA);
		bytes_in_buffer += bytes_written;
		bytes_remaining -= bytes_written;
	}

	list_for_each_safe(start, temp, &driver->cmd_reg_list) {
		item = list_entry(start, struct diag_cmd_reg_t, link);
		if (i < diag_dbgfs_table_index) {
			i++;
			continue;
		}

		is_polling = diag_cmd_chk_polling(&item->entry);
		bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
					  "i: %3d, cmd_code: %4x, subsys_id: %4x, cmd_code_lo: %4x, cmd_code_hi: %4x, proc: %d, process_id: %5d %s\n",
					  i++,
					  item->entry.cmd_code,
					  item->entry.subsys_id,
					  item->entry.cmd_code_lo,
					  item->entry.cmd_code_hi,
					  item->proc,
					  item->pid,
					  (is_polling == DIAG_CMD_POLLING) ?
					  "<-- Polling Cmd" : "");

		bytes_in_buffer += bytes_written;

		/* Check if there is room to add another table entry */
		bytes_remaining = buf_size - bytes_in_buffer;

		if (bytes_remaining < bytes_written)
			break;
	}
	diag_dbgfs_table_index = i;

	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_in_buffer);

	kfree(buf);
	return ret;
}

static ssize_t diag_dbgfs_read_mempool(struct file *file, char __user *ubuf,
						size_t count, loff_t *ppos)
{
	char *buf = NULL;
	int ret = 0;
	int i = 0;
	unsigned int buf_size;
	unsigned int bytes_remaining = 0;
	unsigned int bytes_written = 0;
	unsigned int bytes_in_buffer = 0;
	struct diag_mempool_t *mempool = NULL;

	if (diag_dbgfs_mempool_index >= NUM_MEMORY_POOLS) {
		/* Done. Reset to prepare for future requests */
		diag_dbgfs_mempool_index = 0;
		return 0;
	}

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	bytes_remaining = buf_size;
	bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
			"%-24s\t"
			"%-10s\t"
			"%-5s\t"
			"%-5s\t"
			"%-5s\n",
			"POOL", "HANDLE", "COUNT", "SIZE", "ITEMSIZE");
	bytes_in_buffer += bytes_written;
	bytes_remaining = buf_size - bytes_in_buffer;

	for (i = diag_dbgfs_mempool_index; i < NUM_MEMORY_POOLS; i++) {
		mempool = &diag_mempools[i];
		bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
			"%-24s\t"
			"%-10p\t"
			"%-5d\t"
			"%-5d\t"
			"%-5d\n",
			mempool->name,
			mempool->pool,
			mempool->count,
			mempool->poolsize,
			mempool->itemsize);
		bytes_in_buffer += bytes_written;

		/* Check if there is room to add another table entry */
		bytes_remaining = buf_size - bytes_in_buffer;

		if (bytes_remaining < bytes_written)
			break;
	}
	diag_dbgfs_mempool_index = i+1;
	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_in_buffer);

	kfree(buf);
	return ret;
}

static ssize_t diag_dbgfs_read_usbinfo(struct file *file, char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	char *buf = NULL;
	int ret = 0;
	int i = 0;
	unsigned int buf_size;
	unsigned int bytes_remaining = 0;
	unsigned int bytes_written = 0;
	unsigned int bytes_in_buffer = 0;
	struct diag_usb_info *usb_info = NULL;

	if (diag_dbgfs_usbinfo_index >= NUM_DIAG_USB_DEV) {
		/* Done. Reset to prepare for future requests */
		diag_dbgfs_usbinfo_index = 0;
		return 0;
	}

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	bytes_remaining = buf_size;
	for (i = diag_dbgfs_usbinfo_index; i < NUM_DIAG_USB_DEV; i++) {
		usb_info = &diag_usb[i];
		if (!usb_info->enabled)
			continue;
		bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
			"id: %d\n"
			"name: %s\n"
			"hdl: %p\n"
			"connected: %d\n"
			"diag state: %d\n"
			"enabled: %d\n"
			"mempool: %s\n"
			"read pending: %d\n"
			"read count: %lu\n"
			"write count: %lu\n"
			"read work pending: %d\n"
			"read done work pending: %d\n"
			"connect work pending: %d\n"
			"disconnect work pending: %d\n\n",
			usb_info->id,
			usb_info->name,
			usb_info->hdl,
			atomic_read(&usb_info->connected),
			atomic_read(&usb_info->diag_state),
			usb_info->enabled,
			DIAG_MEMPOOL_GET_NAME(usb_info->mempool),
			atomic_read(&usb_info->read_pending),
			usb_info->read_cnt,
			usb_info->write_cnt,
			work_pending(&usb_info->read_work),
			work_pending(&usb_info->read_done_work),
			work_pending(&usb_info->connect_work),
			work_pending(&usb_info->disconnect_work));
		bytes_in_buffer += bytes_written;

		/* Check if there is room to add another table entry */
		bytes_remaining = buf_size - bytes_in_buffer;

		if (bytes_remaining < bytes_written)
			break;
	}
	diag_dbgfs_usbinfo_index = i+1;
	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_in_buffer);

	kfree(buf);
	return ret;
}

#ifdef CONFIG_DIAGFWD_BRIDGE_CODE
static ssize_t diag_dbgfs_read_hsicinfo(struct file *file, char __user *ubuf,
					size_t count, loff_t *ppos)
{
	char *buf = NULL;
	int ret = 0;
	int i = 0;
	unsigned int buf_size;
	unsigned int bytes_remaining = 0;
	unsigned int bytes_written = 0;
	unsigned int bytes_in_buffer = 0;
	struct diag_hsic_info *hsic_info = NULL;

	if (diag_dbgfs_hsicinfo_index >= NUM_DIAG_USB_DEV) {
		/* Done. Reset to prepare for future requests */
		diag_dbgfs_hsicinfo_index = 0;
		return 0;
	}

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	bytes_remaining = buf_size;
	for (i = diag_dbgfs_hsicinfo_index; i < NUM_HSIC_DEV; i++) {
		hsic_info = &diag_hsic[i];
		if (!hsic_info->enabled)
			continue;
		bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
			"id: %d\n"
			"name: %s\n"
			"bridge index: %s\n"
			"opened: %d\n"
			"enabled: %d\n"
			"suspended: %d\n"
			"mempool: %s\n"
			"read work pending: %d\n"
			"open work pending: %d\n"
			"close work pending: %d\n\n",
			hsic_info->id,
			hsic_info->name,
			DIAG_BRIDGE_GET_NAME(hsic_info->dev_id),
			hsic_info->opened,
			hsic_info->enabled,
			hsic_info->suspended,
			DIAG_MEMPOOL_GET_NAME(hsic_info->mempool),
			work_pending(&hsic_info->read_work),
			work_pending(&hsic_info->open_work),
			work_pending(&hsic_info->close_work));
		bytes_in_buffer += bytes_written;

		/* Check if there is room to add another table entry */
		bytes_remaining = buf_size - bytes_in_buffer;

		if (bytes_remaining < bytes_written)
			break;
	}
	diag_dbgfs_hsicinfo_index = i+1;
	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_in_buffer);

	kfree(buf);
	return ret;
}

static ssize_t diag_dbgfs_read_mhiinfo(struct file *file, char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	char *buf = NULL;
	int ret = 0;
	int i = 0;
	unsigned int buf_size;
	unsigned int bytes_remaining = 0;
	unsigned int bytes_written = 0;
	unsigned int bytes_in_buffer = 0;
	struct diag_mhi_info *mhi_info = NULL;

	if (diag_dbgfs_mhiinfo_index >= NUM_MHI_DEV) {
		/* Done. Reset to prepare for future requests */
		diag_dbgfs_mhiinfo_index = 0;
		return 0;
	}

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	bytes_remaining = buf_size;
	for (i = diag_dbgfs_mhiinfo_index; i < NUM_MHI_DEV; i++) {
		mhi_info = &diag_mhi[i];
		bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
			"id: %d\n"
			"name: %s\n"
			"bridge index: %s\n"
			"mempool: %s\n"
			"read ch opened: %d\n"
			"read ch hdl: %p\n"
			"write ch opened: %d\n"
			"write ch hdl: %p\n"
			"read work pending: %d\n"
			"read done work pending: %d\n"
			"open work pending: %d\n"
			"close work pending: %d\n\n",
			mhi_info->id,
			mhi_info->name,
			DIAG_BRIDGE_GET_NAME(mhi_info->dev_id),
			DIAG_MEMPOOL_GET_NAME(mhi_info->mempool),
			mhi_info->read_ch.opened,
			mhi_info->read_ch.hdl,
			mhi_info->write_ch.opened,
			mhi_info->write_ch.hdl,
			work_pending(&mhi_info->read_work),
			work_pending(&mhi_info->read_done_work),
			work_pending(&mhi_info->open_work),
			work_pending(&mhi_info->close_work));
		bytes_in_buffer += bytes_written;

		/* Check if there is room to add another table entry */
		bytes_remaining = buf_size - bytes_in_buffer;

		if (bytes_remaining < bytes_written)
			break;
	}
	diag_dbgfs_mhiinfo_index = i+1;
	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_in_buffer);

	kfree(buf);
	return ret;
}

static ssize_t diag_dbgfs_read_bridge(struct file *file, char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	char *buf = NULL;
	int ret = 0;
	int i = 0;
	unsigned int buf_size;
	unsigned int bytes_remaining = 0;
	unsigned int bytes_written = 0;
	unsigned int bytes_in_buffer = 0;
	struct diagfwd_bridge_info *info = NULL;

	if (diag_dbgfs_bridgeinfo_index >= NUM_DIAG_USB_DEV) {
		/* Done. Reset to prepare for future requests */
		diag_dbgfs_bridgeinfo_index = 0;
		return 0;
	}

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		pr_err("diag: %s, Error allocating memory\n", __func__);
		return -ENOMEM;
	}

	buf_size = ksize(buf);
	bytes_remaining = buf_size;
	for (i = diag_dbgfs_bridgeinfo_index; i < NUM_REMOTE_DEV; i++) {
		info = &bridge_info[i];
		if (!info->inited)
			continue;
		bytes_written = scnprintf(buf+bytes_in_buffer, bytes_remaining,
			"id: %d\n"
			"name: %s\n"
			"type: %d\n"
			"inited: %d\n"
			"ctxt: %d\n"
			"dev_ops: %p\n"
			"dci_read_buf: %p\n"
			"dci_read_ptr: %p\n"
			"dci_read_len: %d\n\n",
			info->id,
			info->name,
			info->type,
			info->inited,
			info->ctxt,
			info->dev_ops,
			info->dci_read_buf,
			info->dci_read_ptr,
			info->dci_read_len);
		bytes_in_buffer += bytes_written;

		/* Check if there is room to add another table entry */
		bytes_remaining = buf_size - bytes_in_buffer;

		if (bytes_remaining < bytes_written)
			break;
	}
	diag_dbgfs_bridgeinfo_index = i+1;
	*ppos = 0;
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, bytes_in_buffer);

	kfree(buf);
	return ret;
}

const struct file_operations diag_dbgfs_mhiinfo_ops = {
	.read = diag_dbgfs_read_mhiinfo,
};

const struct file_operations diag_dbgfs_hsicinfo_ops = {
	.read = diag_dbgfs_read_hsicinfo,
};

const struct file_operations diag_dbgfs_bridge_ops = {
	.read = diag_dbgfs_read_bridge,
};

#endif

const struct file_operations diag_dbgfs_status_ops = {
	.read = diag_dbgfs_read_status,
};

const struct file_operations diag_dbgfs_table_ops = {
	.read = diag_dbgfs_read_table,
};

const struct file_operations diag_dbgfs_workpending_ops = {
	.read = diag_dbgfs_read_workpending,
};

const struct file_operations diag_dbgfs_mempool_ops = {
	.read = diag_dbgfs_read_mempool,
};

const struct file_operations diag_dbgfs_usbinfo_ops = {
	.read = diag_dbgfs_read_usbinfo,
};

const struct file_operations diag_dbgfs_dcistats_ops = {
	.read = diag_dbgfs_read_dcistats,
};

const struct file_operations diag_dbgfs_power_ops = {
	.read = diag_dbgfs_read_power,
};

int diag_debugfs_init(void)
{
	struct dentry *entry = NULL;

	diag_dbgfs_dent = debugfs_create_dir("diag", 0);
	if (IS_ERR(diag_dbgfs_dent))
		return -ENOMEM;

	entry = debugfs_create_file("status", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_status_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("table", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_table_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("work_pending", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_workpending_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("mempool", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_mempool_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("usbinfo", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_usbinfo_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("dci_stats", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_dcistats_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("power", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_power_ops);
	if (!entry)
		goto err;

#ifdef CONFIG_DIAGFWD_BRIDGE_CODE
	entry = debugfs_create_file("bridge", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_bridge_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("hsicinfo", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_hsicinfo_ops);
	if (!entry)
		goto err;

	entry = debugfs_create_file("mhiinfo", 0444, diag_dbgfs_dent, 0,
				    &diag_dbgfs_mhiinfo_ops);
	if (!entry)
		goto err;
#endif

	diag_dbgfs_table_index = 0;
	diag_dbgfs_mempool_index = 0;
	diag_dbgfs_usbinfo_index = 0;
	diag_dbgfs_hsicinfo_index = 0;
	diag_dbgfs_bridgeinfo_index = 0;
	diag_dbgfs_mhiinfo_index = 0;
	diag_dbgfs_finished = 0;
	diag_dbgfs_dci_data_index = 0;
	diag_dbgfs_dci_finished = 0;

	/* DCI related structures */
	dci_traffic = kzalloc(sizeof(struct diag_dci_data_info) *
				DIAG_DCI_DEBUG_CNT, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(dci_traffic))
		pr_warn("diag: could not allocate memory for dci debug info\n");

	mutex_init(&dci_stat_mutex);
	return 0;
err:
	kfree(dci_traffic);
	debugfs_remove_recursive(diag_dbgfs_dent);
	return -ENOMEM;
}

void diag_debugfs_cleanup(void)
{
	if (diag_dbgfs_dent) {
		debugfs_remove_recursive(diag_dbgfs_dent);
		diag_dbgfs_dent = NULL;
	}

	kfree(dci_traffic);
	mutex_destroy(&dci_stat_mutex);
}
#else
int diag_debugfs_init(void) { return 0; }
void diag_debugfs_cleanup(void) { }
#endif
