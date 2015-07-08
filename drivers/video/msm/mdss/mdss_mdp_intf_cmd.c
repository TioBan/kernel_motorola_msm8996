/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/pm_runtime.h>

#include "mdss_mdp.h"
#include "mdss_panel.h"
#include "mdss_debug.h"
#include "mdss_mdp_trace.h"
#include "mdss_dsi_clk.h"

#define MAX_RECOVERY_TRIALS 10
#define MAX_SESSIONS 2

#define SPLIT_MIXER_OFFSET 0x800
/* wait for at most 2 vsync for lowest refresh rate (24hz) */
#define KOFF_TIMEOUT msecs_to_jiffies(84)

#define STOP_TIMEOUT(hz) msecs_to_jiffies((1000 / hz) * (6 + 2))
#define POWER_COLLAPSE_TIME msecs_to_jiffies(100)
#define CMD_MODE_IDLE_TIMEOUT msecs_to_jiffies(16 * 4)

static DEFINE_MUTEX(cmd_clk_mtx);

struct mdss_mdp_cmd_ctx {
	struct mdss_mdp_ctl *ctl;
	u32 pp_num;
	u8 ref_cnt;
	struct completion stop_comp;
	struct completion readptr_done;
	struct completion pp_done;
	wait_queue_head_t pp_waitq;
	struct list_head vsync_handlers;
	int panel_power_state;
	atomic_t koff_cnt;
	u32 intf_stopped;
	struct mutex mdp_rdptr_lock;
	struct mutex clk_mtx;
	spinlock_t clk_lock;
	spinlock_t koff_lock;
	struct work_struct gate_clk_work;
	struct delayed_work delayed_off_clk_work;
	struct work_struct pp_done_work;
	struct mutex autorefresh_mtx;
	atomic_t pp_done_cnt;

	int autorefresh_pending_frame_cnt;
	bool autorefresh_off_pending;
	bool autorefresh_init;
	int vsync_irq_cnt;

	struct mdss_intf_recovery intf_recovery;
	struct mdss_mdp_cmd_ctx *sync_ctx; /* for partial update */
	u32 pp_timeout_report_cnt;
	int pingpong_split_slave;
	bool pending_mode_switch; /* Used to prevent powering down in stop */
};

struct mdss_mdp_cmd_ctx mdss_mdp_cmd_ctx_list[MAX_SESSIONS];

static int mdss_mdp_cmd_do_notifier(struct mdss_mdp_cmd_ctx *ctx);
static inline void mdss_mdp_cmd_clk_on(struct mdss_mdp_cmd_ctx *ctx);
static inline void mdss_mdp_cmd_clk_off(struct mdss_mdp_cmd_ctx *ctx);
static int mdss_mdp_cmd_wait4pingpong(struct mdss_mdp_ctl *ctl, void *arg);

static bool __mdss_mdp_cmd_is_panel_power_off(struct mdss_mdp_cmd_ctx *ctx)
{
	return mdss_panel_is_power_off(ctx->panel_power_state);
}

static bool __mdss_mdp_cmd_is_panel_power_on_interactive(
		struct mdss_mdp_cmd_ctx *ctx)
{
	return mdss_panel_is_power_on_interactive(ctx->panel_power_state);
}

static inline u32 mdss_mdp_cmd_line_count(struct mdss_mdp_ctl *ctl)
{
	struct mdss_mdp_mixer *mixer;
	u32 cnt = 0xffff;	/* init it to an invalid value */
	u32 init;
	u32 height;

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);

	mixer = mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_LEFT);
	if (!mixer) {
		mixer = mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_RIGHT);
		if (!mixer) {
			mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
			goto exit;
		}
	}

	init = mdss_mdp_pingpong_read(mixer->pingpong_base,
		MDSS_MDP_REG_PP_VSYNC_INIT_VAL) & 0xffff;
	height = mdss_mdp_pingpong_read(mixer->pingpong_base,
		MDSS_MDP_REG_PP_SYNC_CONFIG_HEIGHT) & 0xffff;

	if (height < init) {
		mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
		goto exit;
	}

	cnt = mdss_mdp_pingpong_read(mixer->pingpong_base,
		MDSS_MDP_REG_PP_INT_COUNT_VAL) & 0xffff;

	if (cnt < init)		/* wrap around happened at height */
		cnt += (height - init);
	else
		cnt -= init;

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

	pr_debug("cnt=%d init=%d height=%d\n", cnt, init, height);
exit:
	return cnt;
}

static int mdss_mdp_tearcheck_enable(struct mdss_mdp_ctl *ctl, bool enable)
{
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	struct mdss_mdp_ctl *sctl;
	struct mdss_mdp_pp_tear_check *te;
	struct mdss_mdp_mixer *mixer =
		mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_LEFT);

	if (IS_ERR_OR_NULL(ctl->panel_data)) {
		pr_err("no panel data\n");
		return -ENODEV;
	}

	if (IS_ERR_OR_NULL(mixer)) {
		pr_err("mixer not configured\n");
		return -ENODEV;
	}

	sctl = mdss_mdp_get_split_ctl(ctl);
	te = &ctl->panel_data->panel_info.te;
	mdss_mdp_pingpong_write(mixer->pingpong_base,
		MDSS_MDP_REG_PP_TEAR_CHECK_EN,
		(te ? te->tear_check_en : 0) && enable);

	/*
	 * When there are two controls, driver needs to enable
	 * tear check configuration for both.
	 */
	if (sctl) {
		mixer = mdss_mdp_mixer_get(sctl, MDSS_MDP_MIXER_MUX_LEFT);
		te = &sctl->panel_data->panel_info.te;
		mdss_mdp_pingpong_write(mixer->pingpong_base,
				MDSS_MDP_REG_PP_TEAR_CHECK_EN,
				(te ? te->tear_check_en : 0) && enable);
	}

	/*
	 * In the case of pingpong split, there is no second
	 * control and enables only slave tear check block as
	 * defined in slave_pingpong_base.
	 */
	if (is_pingpong_split(ctl->mfd))
		mdss_mdp_pingpong_write(mdata->slave_pingpong_base,
				MDSS_MDP_REG_PP_TEAR_CHECK_EN,
				(te ? te->tear_check_en : 0) && enable);
	return 0;
}

static int mdss_mdp_cmd_tearcheck_cfg(struct mdss_mdp_mixer *mixer,
		struct mdss_mdp_cmd_ctx *ctx, bool enable)
{
	struct mdss_mdp_pp_tear_check *te = NULL;
	struct mdss_panel_info *pinfo;
	u32 vsync_clk_speed_hz, total_lines, vclks_line, cfg = 0;
	char __iomem *pingpong_base;
	struct mdss_mdp_ctl *ctl = ctx->ctl;
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	if (IS_ERR_OR_NULL(ctl->panel_data)) {
		pr_err("no panel data\n");
		return -ENODEV;
	}

	if (enable) {
		pinfo = &ctl->panel_data->panel_info;
		te = &ctl->panel_data->panel_info.te;

		mdss_mdp_vsync_clk_enable(1);

		vsync_clk_speed_hz =
			mdss_mdp_get_clk_rate(MDSS_CLK_MDP_VSYNC);

		total_lines = mdss_panel_get_vtotal(pinfo);

		total_lines *= pinfo->mipi.frame_rate;

		vclks_line = (total_lines) ? vsync_clk_speed_hz/total_lines : 0;

		cfg = BIT(19);
		if (pinfo->mipi.hw_vsync_mode)
			cfg |= BIT(20);

		if (te->refx100)
			vclks_line = vclks_line * pinfo->mipi.frame_rate *
				100 / te->refx100;
		else {
			pr_warn("refx100 cannot be zero! Use 6000 as default\n");
			vclks_line = vclks_line * pinfo->mipi.frame_rate *
				100 / 6000;
		}

		cfg |= vclks_line;

		pr_debug("%s: yres=%d vclks=%x height=%d init=%d rd=%d start=%d\n",
			__func__, pinfo->yres, vclks_line, te->sync_cfg_height,
			 te->vsync_init_val, te->rd_ptr_irq, te->start_pos);
		pr_debug("thrd_start =%d thrd_cont=%d\n",
			te->sync_threshold_start, te->sync_threshold_continue);
	}

	pingpong_base = mixer->pingpong_base;

	if (ctx->pingpong_split_slave)
		pingpong_base = mdata->slave_pingpong_base;

	mdss_mdp_pingpong_write(pingpong_base,
		MDSS_MDP_REG_PP_SYNC_CONFIG_VSYNC, cfg);
	mdss_mdp_pingpong_write(pingpong_base,
		MDSS_MDP_REG_PP_SYNC_CONFIG_HEIGHT,
		te ? te->sync_cfg_height : 0);
	mdss_mdp_pingpong_write(pingpong_base,
		MDSS_MDP_REG_PP_VSYNC_INIT_VAL,
		te ? te->vsync_init_val : 0);
	mdss_mdp_pingpong_write(pingpong_base,
		MDSS_MDP_REG_PP_RD_PTR_IRQ,
		te ? te->rd_ptr_irq : 0);
	mdss_mdp_pingpong_write(pingpong_base,
		MDSS_MDP_REG_PP_START_POS,
		te ? te->start_pos : 0);
	mdss_mdp_pingpong_write(pingpong_base,
		MDSS_MDP_REG_PP_SYNC_THRESH,
		te ? ((te->sync_threshold_continue << 16) |
		 te->sync_threshold_start) : 0);
	mdss_mdp_pingpong_write(pingpong_base,
		MDSS_MDP_REG_PP_SYNC_WRCOUNT,
		te ? (te->start_pos + te->sync_threshold_start + 1) : 0);

	return 0;
}

static int mdss_mdp_cmd_tearcheck_setup(struct mdss_mdp_cmd_ctx *ctx,
		bool enable)
{
	int rc = 0;
	struct mdss_mdp_mixer *mixer;
	struct mdss_mdp_ctl *ctl = ctx->ctl;

	mixer = mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_LEFT);
	if (mixer) {
		rc = mdss_mdp_cmd_tearcheck_cfg(mixer, ctx, enable);
		if (rc)
			goto err;
	}

	if (!(ctl->opmode & MDSS_MDP_CTL_OP_PACK_3D_ENABLE)) {
		mixer = mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_RIGHT);
		if (mixer)
			rc = mdss_mdp_cmd_tearcheck_cfg(mixer, ctx, enable);
	}
err:
	return rc;
}

/**
 * enum mdp_rsrc_ctl_events - events for the resource control state machine
 * @MDP_RSRC_CTL_EVENT_KICKOFF:
 *	This event happens at NORMAL priority.
 *	Event that signals the start of the transfer, regardless of the
 *	state at which we enter this state (ON/OFF or GATE),
 *	we must ensure that power state is ON when we return from this
 *	event.
 *
 * @MDP_RSRC_CTL_EVENT_PP_DONE:
 *	This event happens at INTERRUPT level.
 *	Event signals the end of the data transfer, when getting this
 *	event we should have been in ON state, since a transfer was
 *	ongoing (if this is not the case, then
 *	there is a bug).
 *	Since this event is received at interrupt ievel, by the end of
 *	the event we haven't changed the power state, but scheduled
 *	work items to do the transition, so by the end of this event:
 *	1. A work item is scheduled to go to gate state as soon as
 *		possible (as soon as scheduler give us the chance)
 *	2. A delayed work is scheduled to go to OFF after
 *		CMD_MODE_IDLE_TIMEOUT time. Power State will be updated
 *		at the end of each work item, to make sure we update
 *		the status once the transition is fully done.
 *
 * @MDP_RSRC_CTL_EVENT_STOP:
 *	This event happens at NORMAL priority.
 *	When we get this event, we are expected to wait to finish any
 *	pending data transfer and turn off all the clocks/resources,
 *	so after return from this event we must be in off
 *	state.
 */
enum mdp_rsrc_ctl_events {
	MDP_RSRC_CTL_EVENT_KICKOFF = 1,
	MDP_RSRC_CTL_EVENT_PP_DONE,
	MDP_RSRC_CTL_EVENT_STOP
};

enum {
	MDP_RSRC_CTL_STATE_OFF,
	MDP_RSRC_CTL_STATE_ON,
	MDP_RSRC_CTL_STATE_GATE,
};

/* helper functions for debugging */
static char *get_sw_event_name(u32 sw_event)
{
	switch (sw_event) {
	case MDP_RSRC_CTL_EVENT_KICKOFF:
		return "KICKOFF";
	case MDP_RSRC_CTL_EVENT_PP_DONE:
		return "PP_DONE";
	case MDP_RSRC_CTL_EVENT_STOP:
		return "STOP";
	default:
		return "UNKNOWN";
	}
}

static char *get_clk_pwr_state_name(u32 pwr_state)
{
	switch (pwr_state) {
	case MDP_RSRC_CTL_STATE_ON:
		return "STATE_ON";
	case MDP_RSRC_CTL_STATE_OFF:
		return "STATE_OFF";
	case MDP_RSRC_CTL_STATE_GATE:
		return "STATE_GATE";
	default:
		return "UNKNOWN";
	}
}

/**
 * mdss_mdp_get_split_display_ctls() - get the display controllers
 * @ctl: Pointer to pointer to the controller used to do the operation.
 *	This can be the pointer to the master or slave of a display with
 *	the MDP_DUAL_LM_DUAL_DISPLAY split mode.
 * @sctl: Pointer to pointer where it is expected to be set the slave
 *	controller. Function does not expect any input parameter here.
 *
 * This function will populate the pointers to pointers with the controllers of
 * the split display ordered such way that the first input parameter will be
 * populated with the master controller and second parameter will be populated
 * with the slave controller, so the caller can assume both controllers are set
 * in the right order after return.
 *
 * This function can only be called for split configuration that uses two
 * controllers, it expects that first pointer is the one passed to do the
 * operation and it can be either the pointer of the master or slave,
 * since is the job of this function to find and accommodate the master/slave
 * controllers accordingly.
 *
 * Return: 0 - succeed, otherwise - fail
 */
int mdss_mdp_get_split_display_ctls(struct mdss_mdp_ctl **ctl,
	struct mdss_mdp_ctl **sctl)
{
	int rc = 0;
	*sctl = NULL;

	if (*ctl == NULL) {
		pr_err("%s invalid ctl\n", __func__);
		rc = -EINVAL;
		goto exit;
	}

	if ((*ctl)->mfd->split_mode == MDP_DUAL_LM_DUAL_DISPLAY) {
		*sctl = mdss_mdp_get_split_ctl(*ctl);
		if (*sctl) {
			/* pointers are in the correct order */
			pr_debug("%s ctls in correct order ctl:%d sctl:%d\n",
				__func__, (*ctl)->num, (*sctl)->num);
			goto exit;
		} else {
			/*
			 * If we have a split display and we didn't find the
			 * Slave controller from the Master, this means that
			 * ctl is the slave controller, so look for the Master
			 */
			*sctl = mdss_mdp_get_main_ctl(*ctl);
			if (!(*sctl)) {
				/*
				 * Bad state, this shouldn't happen, we should
				 * be having both controllers since we are in
				 * dual-lm, dual-display.
				 */
				pr_err("%s cannot find master ctl\n",
					__func__);
				BUG();
			}
			/*
			 * We have both controllers but sctl has the Master,
			 * swap the pointers so we can keep the master in the
			 * ctl pointer and control the order in the power
			 * sequence.
			 */
			pr_debug("ctl is not the master, swap pointers\n");
			swap(*ctl, *sctl);
		}
	} else {
		pr_debug("%s no split mode:%d\n", __func__,
			(*ctl)->mfd->split_mode);
	}
exit:
	return rc;
}

/**
 * mdss_mdp_resource_control() - control the state of mdp resources
 * @ctl: pointer to controller to notify the event.
 * @sw_event: software event to modify the state of the resources.
 *
 * This function implements an state machine to control the state of
 * the mdp resources (clocks, bw, mmu), the states can be ON, OFF and GATE,
 * transition between each state is controlled through the MDP_RSRC_CTL_EVENT_
 * events.
 *
 * Return: 0 - succeed, otherwise - fail
 */
int mdss_mdp_resource_control(struct mdss_mdp_ctl *ctl, u32 sw_event)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(ctl->mfd);
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	struct mdss_mdp_ctl *sctl = NULL;
	struct mdss_mdp_cmd_ctx *ctx, *sctx = NULL;
	u32 status;
	int rc = 0;

	/* Get both controllers in the correct order for dual displays */
	mdss_mdp_get_split_display_ctls(&ctl, &sctl);

	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("%s invalid ctx\n", __func__);
		rc = -EINVAL;
		goto exit;
	}

	if (sctl)
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];

	/* In pingpong split we have single controller, dual context */
	if (ctl->mfd->split_mode == MDP_PINGPONG_SPLIT)
		sctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[SLAVE_CTX];

	pr_debug("%s ctl:%d pwr_state:%s event:%s\n",
		__func__, ctl->num,
		get_clk_pwr_state_name(mdp5_data->resources_state),
		get_sw_event_name(sw_event));

	switch (sw_event) {
	case MDP_RSRC_CTL_EVENT_KICKOFF:
		/*
		 * Cancel any work item pending:
		 * If POWER-OFF was cancel:
		 *	Only UNGATE the clocks (resources should be ON)
		 * If GATE && POWER-OFF were cancel:
		 *	UNGATE and POWER-ON
		 * If only GATE was cancel:
		 *	something can be wrong, OFF should have been
		 *	cancel as well.
		 */

		/* update the active only vote */
		mdata->ao_bw_uc_idx = mdata->curr_bw_uc_idx;

		/* Cancel GATE Work Item */
		if (cancel_work_sync(&ctx->gate_clk_work)) {
			pr_debug("%s gate work canceled\n", __func__);

			if (mdp5_data->resources_state !=
					MDP_RSRC_CTL_STATE_ON)
				pr_debug("%s unexpected power state\n",
					__func__);
		}

		/* Cancel OFF Work Item  */
		if (cancel_delayed_work_sync(&ctx->delayed_off_clk_work)) {
			pr_debug("%s off work canceled\n", __func__);

			if (mdp5_data->resources_state ==
					MDP_RSRC_CTL_STATE_OFF)
				pr_debug("%s unexpected OFF state\n",
					__func__);
		}

		/* Transition OFF->ON || GATE->ON (enable clocks) */
		if ((mdp5_data->resources_state == MDP_RSRC_CTL_STATE_OFF) ||
			(mdp5_data->resources_state ==
			MDP_RSRC_CTL_STATE_GATE)) {

			/* Enable/Ungate DSI clocks and resources */
			mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);

			mdss_mdp_ctl_intf_event /* enable master */
				(ctx->ctl, MDSS_EVENT_PANEL_CLK_CTRL,
				(void *)MDSS_DSI_CLK_ON, true);

			if (sctx) /* then slave */
				mdss_mdp_ctl_intf_event
				 (sctx->ctl, MDSS_EVENT_PANEL_CLK_CTRL,
				 (void *)MDSS_DSI_CLK_ON, true);

			if (mdp5_data->resources_state ==
					MDP_RSRC_CTL_STATE_GATE)
				mdp5_data->resources_state =
					MDP_RSRC_CTL_STATE_ON;
		}

		/* Transition OFF->ON (enable resources)*/
		if (mdp5_data->resources_state ==
				MDP_RSRC_CTL_STATE_OFF) {

			/* Add an extra vote for the ahb bus */
			mdss_update_reg_bus_vote(mdata->reg_bus_clt,
				VOTE_INDEX_19_MHZ);

			/* Enable MDP resources */
			mdss_mdp_cmd_clk_on(ctx);
			if (sctx)
				mdss_mdp_cmd_clk_on(sctx);

			mdp5_data->resources_state = MDP_RSRC_CTL_STATE_ON;
		}

		if (mdp5_data->resources_state != MDP_RSRC_CTL_STATE_ON) {
			/* we must be ON by the end of kickoff */
			pr_err("%s unexpected power state during:%s\n",
				__func__, get_sw_event_name(sw_event));
			BUG();
		}
		break;
	case MDP_RSRC_CTL_EVENT_PP_DONE:
		if (mdp5_data->resources_state != MDP_RSRC_CTL_STATE_ON) {
			pr_err("%s unexpected power state during:%s\n",
				__func__, get_sw_event_name(sw_event));
			BUG();
		}

		/* Check that no pending kickoff is on-going */
		 status = mdss_mdp_ctl_perf_get_transaction_status(ctl);

		/*
		 * Same for the slave controller. for cases where
		 * transaction is only pending in the slave controller.
		 */
		if (sctl)
			status |= mdss_mdp_ctl_perf_get_transaction_status(
				sctl);

		/*
		 * Schedule the work items to shut down only if
		 * 1. no kickoff has been scheduled
		 * 2. no stop command has been started
		 * 3. no autorefresh is enabled
		 * 4. no validate is pending
		 */
		if ((PERF_STATUS_DONE == status) &&
			!ctx->intf_stopped &&
			!ctx->autorefresh_init &&
			!ctl->mfd->validate_pending) {
			pr_debug("schedule release after:%d ms\n",
				jiffies_to_msecs
				(CMD_MODE_IDLE_TIMEOUT));

			/* start work item to gate */
			if (mdata->enable_gate)
				schedule_work(&ctx->gate_clk_work);

			/* start work item to shut down after delay */
			schedule_delayed_work(
					&ctx->delayed_off_clk_work,
					CMD_MODE_IDLE_TIMEOUT);
		}

		break;
	case MDP_RSRC_CTL_EVENT_STOP:

		/* If we are already OFF, just return */
		if (mdp5_data->resources_state ==
				MDP_RSRC_CTL_STATE_OFF) {
			pr_debug("resources already off\n");
			goto exit;
		}

		/* If pp_done is on-going, wait for it to finish */
		mdss_mdp_cmd_wait4pingpong(ctl, NULL);

		if (sctl)
			mdss_mdp_cmd_wait4pingpong(sctl, NULL);

		/*
		 * If a pp_done happened just before the stop,
		 * we can still have some work items running;
		 * cancel any pending works.
		 */

		/* Cancel GATE Work Item */
		if (cancel_work_sync(&ctx->gate_clk_work)) {
			pr_debug("gate work canceled\n");

			if (mdp5_data->resources_state !=
				MDP_RSRC_CTL_STATE_ON)
				pr_debug("%s power state is not ON\n",
					__func__);
		}

		/* Cancel OFF Work Item  */
		if (cancel_delayed_work_sync(&ctx->delayed_off_clk_work)) {
			pr_debug("off work canceled\n");


			if (mdp5_data->resources_state ==
					MDP_RSRC_CTL_STATE_OFF)
				pr_debug("%s unexpected OFF state\n",
					__func__);
		}

		if ((mdp5_data->resources_state == MDP_RSRC_CTL_STATE_ON) ||
				(mdp5_data->resources_state
				== MDP_RSRC_CTL_STATE_GATE)) {

			/* Enable MDP clocks if gated */
			if (mdp5_data->resources_state ==
					MDP_RSRC_CTL_STATE_GATE)
				mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);

			/* First Power off slave DSI (if present) */
			if (sctx)
				mdss_mdp_cmd_clk_off(sctx);

			/* Now Power off master DSI */
			mdss_mdp_cmd_clk_off(ctx);

			/* Remove extra vote for the ahb bus */
			mdss_update_reg_bus_vote(mdata->reg_bus_clt,
				VOTE_INDEX_DISABLE);


			/* we are done accessing the resources */
			mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

			/* update the state, now we are in off */
			mdp5_data->resources_state = MDP_RSRC_CTL_STATE_OFF;
		}
		break;
	default:
		pr_warn("%s unexpected event (%d)\n", __func__, sw_event);
		break;
	}

exit:
	return rc;
}


static inline void mdss_mdp_cmd_clk_on(struct mdss_mdp_cmd_ctx *ctx)
{
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	if (__mdss_mdp_cmd_is_panel_power_off(ctx))
		return;

	mutex_lock(&ctx->clk_mtx);
	MDSS_XLOG(ctx->pp_num, atomic_read(&ctx->koff_cnt));

	mdss_bus_bandwidth_ctrl(true);

	mdss_mdp_hist_intr_setup(&mdata->hist_intr, MDSS_IRQ_RESUME);

	mutex_unlock(&ctx->clk_mtx);
}

static inline void mdss_mdp_cmd_clk_off(struct mdss_mdp_cmd_ctx *ctx)
{
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	if (ctx->autorefresh_init) {
		/* Do not turn off clocks if aurtorefresh is on. */
		return;
	}

	mutex_lock(&ctx->clk_mtx);
	MDSS_XLOG(ctx->pp_num, atomic_read(&ctx->koff_cnt));

	mdss_mdp_hist_intr_setup(&mdata->hist_intr, MDSS_IRQ_SUSPEND);

	/* Power off DSI, is caller responsibility to do slave then master  */
	if (ctx->ctl) {
		mdss_mdp_ctl_intf_event
			(ctx->ctl, MDSS_EVENT_PANEL_CLK_CTRL,
			(void *) MDSS_DSI_CLK_OFF, true);
	} else {
		pr_err("OFF with ctl:NULL\n");
	}

	mdss_bus_bandwidth_ctrl(false);

	mutex_unlock(&ctx->clk_mtx);
}

static void mdss_mdp_cmd_readptr_done(void *arg)
{
	struct mdss_mdp_ctl *ctl = arg;
	struct mdss_mdp_cmd_ctx *ctx = ctl->intf_ctx[MASTER_CTX];
	struct mdss_mdp_vsync_handler *tmp;
	ktime_t vsync_time;

	if (!ctx) {
		pr_err("invalid ctx\n");
		return;
	}

	if (ctx->autorefresh_init) {
		pr_debug("Completing read pointer done\n");
		complete_all(&ctx->readptr_done);
	}

	if (ctx->autorefresh_off_pending)
		ctx->autorefresh_off_pending = false;

	vsync_time = ktime_get();
	ctl->vsync_cnt++;
	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt));

	spin_lock(&ctx->clk_lock);
	list_for_each_entry(tmp, &ctx->vsync_handlers, list) {
		if (tmp->enabled && !tmp->cmd_post_flush)
			tmp->vsync_handler(ctl, vsync_time);
	}
	spin_unlock(&ctx->clk_lock);
}

static void mdss_mdp_cmd_intf_recovery(void *data, int event)
{
	struct mdss_mdp_cmd_ctx *ctx = data;
	unsigned long flags;

	if (!data) {
		pr_err("%s: invalid ctx\n", __func__);
		return;
	}

	if (!ctx->ctl)
		return;

	/*
	 * Currently, only intf_fifo_underflow is
	 * supported for recovery sequence for command
	 * mode DSI interface
	 */
	if (event != MDP_INTF_DSI_CMD_FIFO_UNDERFLOW) {
		pr_warn("%s: unsupported recovery event:%d\n",
					__func__, event);
		return;
	}

	spin_lock_irqsave(&ctx->koff_lock, flags);
	if (atomic_read(&ctx->koff_cnt)) {
		mdss_mdp_ctl_reset(ctx->ctl);
		pr_debug("%s: intf_num=%d\n", __func__,
					ctx->ctl->intf_num);
		atomic_dec(&ctx->koff_cnt);
		mdss_mdp_irq_disable_nosync(MDSS_MDP_IRQ_PING_PONG_COMP,
						ctx->pp_num);
	}
	spin_unlock_irqrestore(&ctx->koff_lock, flags);
}

static void mdss_mdp_cmd_pingpong_done(void *arg)
{
	struct mdss_mdp_ctl *ctl = arg;
	struct mdss_mdp_cmd_ctx *ctx = ctl->intf_ctx[MASTER_CTX];
	struct mdss_mdp_vsync_handler *tmp;
	ktime_t vsync_time;
	u32 status;

	if (!ctx) {
		pr_err("%s: invalid ctx\n", __func__);
		return;
	}

	mdss_mdp_ctl_perf_set_transaction_status(ctl,
		PERF_HW_MDP_STATE, PERF_STATUS_DONE);

	spin_lock(&ctx->clk_lock);
	list_for_each_entry(tmp, &ctx->vsync_handlers, list) {
		if (tmp->enabled && tmp->cmd_post_flush)
			tmp->vsync_handler(ctl, vsync_time);
	}
	spin_unlock(&ctx->clk_lock);

	spin_lock(&ctx->koff_lock);
	mdss_mdp_irq_disable_nosync(MDSS_MDP_IRQ_PING_PONG_COMP, ctx->pp_num);
	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt));

	if (atomic_add_unless(&ctx->koff_cnt, -1, 0)) {
		if (atomic_read(&ctx->koff_cnt))
			pr_err("%s: too many kickoffs=%d!\n", __func__,
			       atomic_read(&ctx->koff_cnt));
		if (mdss_mdp_cmd_do_notifier(ctx)) {
			atomic_inc(&ctx->pp_done_cnt);
			status = mdss_mdp_ctl_perf_get_transaction_status(ctl);
			if (status == 0)
				schedule_work(&ctx->pp_done_work);

			mdss_mdp_resource_control(ctl,
				MDP_RSRC_CTL_EVENT_PP_DONE);
		}
		wake_up_all(&ctx->pp_waitq);
	} else if (!ctl->cmd_autorefresh_en) {
		pr_err("%s: should not have pingpong interrupt!\n", __func__);
	}

	trace_mdp_cmd_pingpong_done(ctl, ctx->pp_num,
					atomic_read(&ctx->koff_cnt));
	pr_debug("%s: ctl_num=%d intf_num=%d ctx=%d kcnt=%d\n", __func__,
			ctl->num, ctl->intf_num, ctx->pp_num,
				atomic_read(&ctx->koff_cnt));

	spin_unlock(&ctx->koff_lock);
}

static void pingpong_done_work(struct work_struct *work)
{
	struct mdss_mdp_cmd_ctx *ctx =
		container_of(work, typeof(*ctx), pp_done_work);

	if (ctx->ctl) {
		while (atomic_add_unless(&ctx->pp_done_cnt, -1, 0))
			mdss_mdp_ctl_notify(ctx->ctl, MDP_NOTIFY_FRAME_DONE);

		mdss_mdp_ctl_perf_release_bw(ctx->ctl);
	}
}

static void clk_ctrl_delayed_off_work(struct work_struct *work)
{
	struct mdss_overlay_private *mdp5_data;
	struct delayed_work *dw = to_delayed_work(work);
	struct mdss_mdp_cmd_ctx *ctx = container_of(dw,
		struct mdss_mdp_cmd_ctx, delayed_off_clk_work);
	struct mdss_mdp_ctl *ctl, *sctl;
	struct mdss_mdp_cmd_ctx *sctx = NULL;
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	if (!ctx) {
		pr_err("%s: invalid ctx\n", __func__);
		return;
	}

	ctl = ctx->ctl;
	if (!ctl || !ctl->panel_data) {
		pr_err("NULL ctl||panel_data\n");
		return;
	}

	mdp5_data = mfd_to_mdp5_data(ctl->mfd);
	ATRACE_BEGIN(__func__);

	/*
	 * Ideally we should not wait for the gate work item to finish, since
	 * this work happens CMD_MODE_IDLE_TIMEOUT time after,
	 * but if the system is laggy, prevent from a race condition
	 * between both work items by waiting for the gate to finish.
	 */
	if (mdata->enable_gate)
		flush_work(&ctx->gate_clk_work);

	pr_debug("ctl:%d pwr_state:%s\n", ctl->num,
		get_clk_pwr_state_name
		(mdp5_data->resources_state));

	if (ctl->mfd->split_mode == MDP_DUAL_LM_DUAL_DISPLAY) {
		if (mdss_mdp_get_split_display_ctls(&ctl, &sctl)) {
			/* error when getting both controllers, just returnr */
			pr_err("cannot get both controllers for the split display\n");
			return;
		}

		/* re-assign to have the correct order in the context */
		ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];
		if (!ctx || !sctl) {
			pr_err("invalid %s %s\n",
				ctx?"":"ctx", sctx?"":"sctx");
			return;
		}
		mutex_lock(&cmd_clk_mtx);
	}

	/* Enable clocks if Gate feature is enabled and we are in this state */
	if (mdata->enable_gate) {
		/* clocks are gated at this time */
		mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);

		if (mdp5_data->resources_state
			!= MDP_RSRC_CTL_STATE_GATE)
			pr_warn("enter off from unexpected state\n");
	}

	/* first power off the slave DSI  (if present) */
	if (sctx)
		mdss_mdp_cmd_clk_off(sctx);

	/* now power off the master DSI */
	mdss_mdp_cmd_clk_off(ctx);

	/* Remove extra vote for the ahb bus */
	mdss_update_reg_bus_vote(mdata->reg_bus_clt,
		VOTE_INDEX_DISABLE);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

	/* update state machine that power off transition is done */
	mdp5_data->resources_state = MDP_RSRC_CTL_STATE_OFF;

	/* do this at the end, so we can also protect the global power state*/
	if (ctl->mfd->split_mode == MDP_DUAL_LM_DUAL_DISPLAY)
		mutex_unlock(&cmd_clk_mtx);

	ATRACE_END(__func__);
}

static void clk_ctrl_gate_work(struct work_struct *work)
{
	struct mdss_overlay_private *mdp5_data;
	struct mdss_mdp_cmd_ctx *ctx =
		container_of(work, typeof(*ctx), gate_clk_work);
	struct mdss_mdp_ctl *ctl, *sctl;
	struct mdss_mdp_cmd_ctx *sctx = NULL;

	if (!ctx) {
		pr_err("%s: invalid ctx\n", __func__);
		return;
	}

	ATRACE_BEGIN(__func__);
	ctl = ctx->ctl;
	if (!ctl) {
		pr_err("%s: invalid ctl\n", __func__);
		return;
	}

	mdp5_data = mfd_to_mdp5_data(ctl->mfd);
	if (!mdp5_data) {
		pr_err("%s: invalid mdp data\n", __func__);
		return;
	}

	pr_debug("%s ctl:%d pwr_state:%s\n", __func__,
		ctl->num, get_clk_pwr_state_name
		(mdp5_data->resources_state));

	if (ctl->mfd->split_mode == MDP_DUAL_LM_DUAL_DISPLAY) {
		if (mdss_mdp_get_split_display_ctls(&ctl, &sctl)) {
			/* error when getting both controllers, just return */
			pr_err("%s cannot get both cts for the split display\n",
				__func__);
			return;
		}

		/* re-assign to have the correct order in the context */
		ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];
		if (!ctx || !sctx) {
			pr_err("%s ERROR invalid %s %s\n", __func__,
				ctx?"":"ctx", sctx?"":"sctx");
			return;
		}
		mutex_lock(&cmd_clk_mtx);
	}

	/* First gate the DSI clocks for the slave controller (if present) */
	if (sctx)
		mdss_mdp_ctl_intf_event
			(sctx->ctl, MDSS_EVENT_PANEL_CLK_CTRL,
			(void *)MDSS_DSI_CLK_EARLY_GATE, true);

	/* Now gate DSI clocks for the master */
	mdss_mdp_ctl_intf_event
		(ctx->ctl, MDSS_EVENT_PANEL_CLK_CTRL,
		(void *)MDSS_DSI_CLK_EARLY_GATE, true);

	/* Gate mdp clocks */
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

	/* update state machine that gate transition is done */
	mdp5_data->resources_state = MDP_RSRC_CTL_STATE_GATE;

	/* unlock mutex needed for split display */
	if (ctl->mfd->split_mode == MDP_DUAL_LM_DUAL_DISPLAY)
		mutex_unlock(&cmd_clk_mtx);

	ATRACE_END(__func__);
}

static int mdss_mdp_setup_vsync(struct mdss_mdp_cmd_ctx *ctx,
	bool enable)
{
	int changed = 0;

	mutex_lock(&ctx->mdp_rdptr_lock);

	if (enable) {
		if (ctx->vsync_irq_cnt == 0)
			changed++;
		ctx->vsync_irq_cnt++;
	} else {
		if (ctx->vsync_irq_cnt) {
			ctx->vsync_irq_cnt--;
			if (ctx->vsync_irq_cnt == 0)
				changed++;
		} else {
			pr_warn("%pS->%s: rd_ptr can not be turned off\n",
				__builtin_return_address(0), __func__);
		}
	}

	if (changed)
		MDSS_XLOG(ctx->vsync_irq_cnt, enable, current->pid);

	pr_debug("%pS->%s: vsync_cnt=%d changed=%d enable=%d ctl:%d pp:%d\n",
			__builtin_return_address(0), __func__,
			ctx->vsync_irq_cnt, changed, enable,
			ctx->ctl->num, ctx->pp_num);

	if (changed) {
		if (enable) {
			/* enable clocks and irq */
			mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);
			mdss_mdp_irq_enable(MDSS_MDP_IRQ_PING_PONG_RD_PTR,
				ctx->pp_num);
		} else {
			/* disable clocks and irq */
			mdss_mdp_irq_disable(MDSS_MDP_IRQ_PING_PONG_RD_PTR,
				ctx->pp_num);
			mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
		}
	}

	mutex_unlock(&ctx->mdp_rdptr_lock);
	return ctx->vsync_irq_cnt;
}

static int mdss_mdp_cmd_add_vsync_handler(struct mdss_mdp_ctl *ctl,
		struct mdss_mdp_vsync_handler *handle)
{
	struct mdss_mdp_ctl *sctl = NULL;
	struct mdss_mdp_cmd_ctx *ctx, *sctx = NULL;
	unsigned long flags;
	bool enable_rdptr = false;

	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("%s: invalid ctx\n", __func__);
		return -ENODEV;
	}

	pr_debug("%pS->%s ctl:%d\n",
		__builtin_return_address(0), __func__, ctl->num);

	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt));
	sctl = mdss_mdp_get_split_ctl(ctl);
	if (sctl)
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];

	spin_lock_irqsave(&ctx->clk_lock, flags);
	if (!handle->enabled) {
		handle->enabled = true;
		list_add(&handle->list, &ctx->vsync_handlers);

		enable_rdptr = !handle->cmd_post_flush;
	}
	spin_unlock_irqrestore(&ctx->clk_lock, flags);

	if (enable_rdptr) {
		if (ctl->panel_data->panel_info.is_split_display)
			mutex_lock(&cmd_clk_mtx);

		/* enable rd_ptr interrupt and clocks */
		mdss_mdp_setup_vsync(ctx, true);

		if (ctl->panel_data->panel_info.is_split_display)
			mutex_unlock(&cmd_clk_mtx);
	}

	return 0;
}

static int mdss_mdp_cmd_remove_vsync_handler(struct mdss_mdp_ctl *ctl,
		struct mdss_mdp_vsync_handler *handle)
{
	struct mdss_mdp_ctl *sctl;
	struct mdss_mdp_cmd_ctx *ctx, *sctx = NULL;
	unsigned long flags;
	bool disable_vsync_irq = false;

	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("%s: invalid ctx\n", __func__);
		return -ENODEV;
	}

	pr_debug("%pS->%s ctl:%d\n",
		__builtin_return_address(0), __func__, ctl->num);

	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt), 0x88888);
	sctl = mdss_mdp_get_split_ctl(ctl);
	if (sctl)
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];

	spin_lock_irqsave(&ctx->clk_lock, flags);
	if (handle->enabled) {
		handle->enabled = false;
		list_del_init(&handle->list);
		disable_vsync_irq = !handle->cmd_post_flush;
	}
	spin_unlock_irqrestore(&ctx->clk_lock, flags);

	if (disable_vsync_irq) {
		/* disable rd_ptr interrupt and clocks */
		mdss_mdp_setup_vsync(ctx, false);
		complete(&ctx->stop_comp);
	}

	return 0;
}

int mdss_mdp_cmd_reconfigure_splash_done(struct mdss_mdp_ctl *ctl,
	bool handoff)
{
	struct mdss_panel_data *pdata;
	struct mdss_mdp_ctl *sctl = mdss_mdp_get_split_ctl(ctl);
	int ret = 0;

	pdata = ctl->panel_data;

	if (sctl)
		mdss_mdp_ctl_intf_event(sctl, MDSS_EVENT_PANEL_CLK_CTRL,
			(void *)MDSS_DSI_CLK_OFF, true);

	mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_PANEL_CLK_CTRL,
		(void *)MDSS_DSI_CLK_OFF, true);

	pdata->panel_info.cont_splash_enabled = 0;
	if (sctl)
		sctl->panel_data->panel_info.cont_splash_enabled = 0;
	else if (pdata->next && is_pingpong_split(ctl->mfd))
		pdata->next->panel_info.cont_splash_enabled = 0;

	return ret;
}

static int mdss_mdp_cmd_wait4pingpong(struct mdss_mdp_ctl *ctl, void *arg)
{
	struct mdss_mdp_cmd_ctx *ctx;
	struct mdss_panel_data *pdata;
	unsigned long flags;
	int rc = 0;

	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("invalid ctx\n");
		return -ENODEV;
	}

	pdata = ctl->panel_data;

	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt), ctl->roi_bkup.w,
			ctl->roi_bkup.h);

	pr_debug("%s: intf_num=%d ctx=%p koff_cnt=%d\n", __func__,
			ctl->intf_num, ctx, atomic_read(&ctx->koff_cnt));

	rc = wait_event_timeout(ctx->pp_waitq,
			atomic_read(&ctx->koff_cnt) == 0,
			KOFF_TIMEOUT);

	trace_mdp_cmd_wait_pingpong(ctl->num,
				atomic_read(&ctx->koff_cnt));

	if (rc <= 0) {
		u32 status, mask;

		mask = BIT(MDSS_MDP_IRQ_PING_PONG_COMP + ctx->pp_num);
		status = mask & readl_relaxed(ctl->mdata->mdp_base +
				MDSS_MDP_REG_INTR_STATUS);
		if (status) {
			pr_warn("pp done but irq not triggered\n");
			mdss_mdp_irq_clear(ctl->mdata,
					MDSS_MDP_IRQ_PING_PONG_COMP,
					ctx->pp_num);
			local_irq_save(flags);
			mdss_mdp_cmd_pingpong_done(ctl);
			local_irq_restore(flags);
			rc = 1;
		}

		rc = atomic_read(&ctx->koff_cnt) == 0;
	}

	if (rc <= 0) {
		if (ctx->pp_timeout_report_cnt == 0) {
			MDSS_XLOG_TOUT_HANDLER("mdp", "dsi0_ctrl", "dsi0_phy",
				"dsi1_ctrl", "dsi1_phy");
		} else if (ctx->pp_timeout_report_cnt == MAX_RECOVERY_TRIALS) {
			WARN(1, "cmd kickoff timed out (%d) ctl=%d\n",
					rc, ctl->num);
			MDSS_XLOG_TOUT_HANDLER("mdp", "dsi0_ctrl", "dsi0_phy",
				"dsi1_ctrl", "dsi1_phy", "panic");
			mdss_fb_report_panel_dead(ctl->mfd);
		}
		ctx->pp_timeout_report_cnt++;
		rc = -EPERM;
		mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_TIMEOUT);
		atomic_add_unless(&ctx->koff_cnt, -1, 0);
	} else {
		rc = 0;
		ctx->pp_timeout_report_cnt = 0;
	}

	cancel_work_sync(&ctx->pp_done_work);

	/* signal any pending ping pong done events */
	while (atomic_add_unless(&ctx->pp_done_cnt, -1, 0))
		mdss_mdp_ctl_notify(ctx->ctl, MDP_NOTIFY_FRAME_DONE);

	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt), rc);

	return rc;
}

static int mdss_mdp_cmd_do_notifier(struct mdss_mdp_cmd_ctx *ctx)
{
	struct mdss_mdp_cmd_ctx *sctx;

	sctx = ctx->sync_ctx;
	if (!sctx || atomic_read(&sctx->koff_cnt) == 0)
		return 1;

	return 0;
}

static void mdss_mdp_cmd_set_sync_ctx(
		struct mdss_mdp_ctl *ctl, struct mdss_mdp_ctl *sctl)
{
	struct mdss_mdp_cmd_ctx *ctx, *sctx;

	ctx = (struct mdss_mdp_cmd_ctx *)ctl->intf_ctx[MASTER_CTX];

	if (!sctl) {
		ctx->sync_ctx = NULL;
		return;
	}

	sctx = (struct mdss_mdp_cmd_ctx *)sctl->intf_ctx[MASTER_CTX];

	if (!sctl->roi.w && !sctl->roi.h) {
		/* left only */
		ctx->sync_ctx = NULL;
		sctx->sync_ctx = NULL;
	} else  {
		/* left + right */
		ctx->sync_ctx = sctx;
		sctx->sync_ctx = ctx;
	}
}

static int mdss_mdp_cmd_set_partial_roi(struct mdss_mdp_ctl *ctl)
{
	int rc = 0;

	if (!ctl->panel_data->panel_info.partial_update_supported)
		return rc;

	/* set panel col and page addr */
	rc = mdss_mdp_ctl_intf_event(ctl,
			MDSS_EVENT_ENABLE_PARTIAL_ROI, NULL, false);
	return rc;
}

static int mdss_mdp_cmd_set_stream_size(struct mdss_mdp_ctl *ctl)
{
	int rc = 0;

	if (!ctl->panel_data->panel_info.partial_update_supported)
		return rc;

	/* set dsi controller stream size */
	rc = mdss_mdp_ctl_intf_event(ctl,
			MDSS_EVENT_DSI_STREAM_SIZE, NULL, false);
	return rc;
}

static int mdss_mdp_cmd_panel_on(struct mdss_mdp_ctl *ctl,
	struct mdss_mdp_ctl *sctl)
{
	struct mdss_mdp_cmd_ctx *ctx, *sctx = NULL;
	int rc = 0;

	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("invalid ctx\n");
		return -ENODEV;
	}

	if (sctl)
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];

	if (!__mdss_mdp_cmd_is_panel_power_on_interactive(ctx)) {
		rc = mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_LINK_READY, NULL,
			false);
		WARN(rc, "intf %d link ready error (%d)\n", ctl->intf_num, rc);

		rc = mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_UNBLANK, NULL,
			false);
		WARN(rc, "intf %d unblank error (%d)\n", ctl->intf_num, rc);

		rc = mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_PANEL_ON, NULL,
			false);
		WARN(rc, "intf %d panel on error (%d)\n", ctl->intf_num, rc);

		rc = mdss_mdp_tearcheck_enable(ctl, true);
		WARN(rc, "intf %d tearcheck enable error (%d)\n",
				ctl->intf_num, rc);

		ctx->panel_power_state = MDSS_PANEL_POWER_ON;
		if (sctx)
			sctx->panel_power_state = MDSS_PANEL_POWER_ON;

		mdss_mdp_ctl_intf_event(ctl,
			MDSS_EVENT_REGISTER_RECOVERY_HANDLER,
			(void *)&ctx->intf_recovery, false);

		ctx->intf_stopped = 0;
	} else {
		pr_err("%s: Panel already on\n", __func__);
	}

	return rc;
}

static int __mdss_mdp_cmd_configure_autorefresh(struct mdss_mdp_ctl *ctl, int
		frame_cnt, bool delayed)
{
	struct mdss_mdp_cmd_ctx *ctx;
	bool enable = frame_cnt ? true : false;

	if (!ctl || !ctl->mixer_left) {
		pr_err("invalid ctl structure\n");
		return -ENODEV;
	}
	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("invalid ctx\n");
		return -ENODEV;
	}

	if (frame_cnt == ctl->autorefresh_frame_cnt) {
		pr_debug("No change to the refresh count\n");
		return 0;
	}
	pr_debug("%s enable = %d frame_cnt = %d init=%d\n", __func__,
			enable, frame_cnt, ctx->autorefresh_init);

	mutex_lock(&ctx->autorefresh_mtx);

	if (enable) {
		if (delayed) {
			ctx->autorefresh_pending_frame_cnt = frame_cnt;
		} else {
			if (!ctx->autorefresh_init) {
				/*
				 * clocks and resources were powered on
				 * during kickoff and following flag
				 * will prevent to schedule work
				 * items to release resources.
				 */
				ctx->autorefresh_init = true;
			}
			mdss_mdp_pingpong_write(ctl->mixer_left->pingpong_base,
					MDSS_MDP_REG_PP_AUTOREFRESH_CONFIG,
					BIT(31) | frame_cnt);
			/*
			 * This manual kickoff is needed to actually start the
			 * autorefresh feature. The h/w relies on one commit
			 * before it starts counting the read ptrs to trigger
			 * the frames.
			 */
			mdss_mdp_ctl_write(ctl, MDSS_MDP_REG_CTL_START, 1);
			ctl->autorefresh_frame_cnt = frame_cnt;
			ctl->cmd_autorefresh_en = true;
		}
	} else {
		if (ctx->autorefresh_init) {
			/*
			 * Safe to turn off the feature. The clocks will be on
			 * at this time since the feature was enabled.
			 */

			mdss_mdp_pingpong_write(ctl->mixer_left->pingpong_base,
					MDSS_MDP_REG_PP_AUTOREFRESH_CONFIG, 0);
		}

		ctx->autorefresh_init = false;
		ctx->autorefresh_pending_frame_cnt = 0;
		ctx->autorefresh_off_pending = true;

		ctl->autorefresh_frame_cnt = 0;
		ctl->cmd_autorefresh_en = false;
	}

	mutex_unlock(&ctx->autorefresh_mtx);

	return 0;
}

/*
 * This function will be called from the sysfs node to enable and disable the
 * feature.
 */
int mdss_mdp_cmd_set_autorefresh_mode(struct mdss_mdp_ctl *ctl, int frame_cnt)
{
	return __mdss_mdp_cmd_configure_autorefresh(ctl, frame_cnt, true);
}

/*
 * This function is called from the commit thread. This function will check if
 * there was are any pending requests from the sys fs node for the feature and
 * if so then it will enable in the h/w.
 */
static int mdss_mdp_cmd_enable_cmd_autorefresh(struct mdss_mdp_ctl *ctl,
	int frame_cnt)
{
	return __mdss_mdp_cmd_configure_autorefresh(ctl, frame_cnt, false);
}

/*
 * There are 3 partial update possibilities
 * left only ==> enable left pingpong_done
 * left + right ==> enable both pingpong_done
 * right only ==> enable right pingpong_done
 *
 * notification is triggered at pingpong_done which will
 * signal timeline to release source buffer
 *
 * for left+right case, pingpong_done is enabled for both and
 * only the last pingpong_done should trigger the notification
 */
int mdss_mdp_cmd_kickoff(struct mdss_mdp_ctl *ctl, void *arg)
{
	struct mdss_mdp_ctl *sctl = NULL;
	struct mdss_mdp_cmd_ctx *ctx, *sctx = NULL;

	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("invalid ctx\n");
		return -ENODEV;
	}

	if (ctx->intf_stopped) {
		pr_err("ctx=%d stopped already\n", ctx->pp_num);
		return -EPERM;
	}

	reinit_completion(&ctx->readptr_done);
	/* sctl will be null for right only in the case of Partial update */
	sctl = mdss_mdp_get_split_ctl(ctl);

	if (sctl && (sctl->roi.w == 0 || sctl->roi.h == 0)) {
		/* left update only */
		sctl = NULL;
	}

	mdss_mdp_ctl_perf_set_transaction_status(ctl,
		PERF_HW_MDP_STATE, PERF_STATUS_BUSY);

	if (sctl) {
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];
		mdss_mdp_ctl_perf_set_transaction_status(sctl,
			PERF_HW_MDP_STATE, PERF_STATUS_BUSY);
	}

	/*
	 * Turn on the panel, if not already. This is because the panel is
	 * turned on only when we send the first frame and not during cmd
	 * start. This is to ensure that no artifacts are seen on the panel.
	 */
	if (__mdss_mdp_cmd_is_panel_power_off(ctx))
		mdss_mdp_cmd_panel_on(ctl, sctl);

	MDSS_XLOG(ctl->num, ctl->roi.x, ctl->roi.y, ctl->roi.w,
						ctl->roi.h);

	atomic_inc(&ctx->koff_cnt);
	if (sctx)
		atomic_inc(&sctx->koff_cnt);

	trace_mdp_cmd_kickoff(ctl->num, atomic_read(&ctx->koff_cnt));

	/*
	 * Call state machine with kickoff event, we just do it for
	 * current CTL, but internally state machine will check and
	 * if this is a dual dsi, it will enable the power resources
	 * for both DSIs
	 */
	mdss_mdp_resource_control(ctl, MDP_RSRC_CTL_EVENT_KICKOFF);

	mdss_mdp_cmd_set_partial_roi(ctl);

	/*
	 * tx dcs command if had any
	 */
	mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_DSI_CMDLIST_KOFF, NULL, false);

	mdss_mdp_cmd_set_stream_size(ctl);

	mdss_mdp_cmd_set_sync_ctx(ctl, sctl);

	if (ctx->autorefresh_init || ctx->autorefresh_off_pending) {
		/*
		 * If autorefresh is enabled then do not queue the frame till
		 * the next read ptr is done otherwise we might get a pp done
		 * immediately for the past autorefresh frame instead.
		 * Clocks and resources were already powered on during kickoff,
		 * and "autorefresh_init" flag will prevent from release
		 * resources during pp done.
		 */
		pr_debug("Wait for read pointer done before enabling PP irq\n");
		wait_for_completion(&ctx->readptr_done);
	}

	mdss_mdp_irq_enable(MDSS_MDP_IRQ_PING_PONG_COMP, ctx->pp_num);
	if (sctx)
		mdss_mdp_irq_enable(MDSS_MDP_IRQ_PING_PONG_COMP, sctx->pp_num);

	if (!ctx->autorefresh_pending_frame_cnt && !ctl->cmd_autorefresh_en) {
		/* Kickoff */
		mdss_mdp_ctl_write(ctl, MDSS_MDP_REG_CTL_START, 1);
	} else {
		pr_debug("Enabling autorefresh in hardware.\n");
		mdss_mdp_cmd_enable_cmd_autorefresh(ctl,
				ctx->autorefresh_pending_frame_cnt);
	}

	mdss_mdp_ctl_perf_set_transaction_status(ctl,
		PERF_SW_COMMIT_STATE, PERF_STATUS_DONE);

	if (sctl) {
		mdss_mdp_ctl_perf_set_transaction_status(sctl,
			PERF_SW_COMMIT_STATE, PERF_STATUS_DONE);
	}

	mb();
	MDSS_XLOG(ctl->num,  atomic_read(&ctx->koff_cnt));
	return 0;
}

int mdss_mdp_cmd_restore(struct mdss_mdp_ctl *ctl)
{
	pr_debug("%s: called for ctl%d\n", __func__, ctl->num);
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);
	if (mdss_mdp_cmd_tearcheck_setup(ctl->intf_ctx[MASTER_CTX], true))
		pr_warn("%s: tearcheck setup failed\n", __func__);
	else
		mdss_mdp_tearcheck_enable(ctl, true);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

	return 0;
}

int mdss_mdp_cmd_ctx_stop(struct mdss_mdp_ctl *ctl,
		struct mdss_mdp_cmd_ctx *ctx, int panel_power_state,
		bool pend_switch)
{
	struct mdss_mdp_cmd_ctx *sctx = NULL;
	struct mdss_mdp_ctl *sctl = NULL;

	sctl = mdss_mdp_get_split_ctl(ctl);
	if (sctl)
		sctx = (struct mdss_mdp_cmd_ctx *) sctl->intf_ctx[MASTER_CTX];

	/* intf stopped,  no more kickoff */
	ctx->intf_stopped = 1;

	/*
	 * if any vsyncs are still enabled, loop until the refcount
	 * goes to zero, so the rd ptr interrupt is disabled.
	 * Ideally this shouldn't be the case since vsync handlers
	 * has been flushed by now, so issue a warning in case
	 * that we hit this condition.
	 */
	if (ctx->vsync_irq_cnt) {
		WARN(1, "vsync still enabled\n");
		while (mdss_mdp_setup_vsync(ctx, false))
			;
	}

	if (!pend_switch) {
		mdss_mdp_ctl_intf_event(ctl,
			MDSS_EVENT_REGISTER_RECOVERY_HANDLER,
			NULL,
			false);
	}

	/* shut down the MDP/DSI resources if still enabled */
	mdss_mdp_resource_control(ctl, MDP_RSRC_CTL_EVENT_STOP);

	flush_work(&ctx->pp_done_work);
	mdss_mdp_cmd_tearcheck_setup(ctx, false);
	mdss_mdp_tearcheck_enable(ctl, false);

	if (mdss_panel_is_power_on(panel_power_state)) {
		pr_debug("%s: intf stopped with panel on\n", __func__);
		return 0;
	}

	mdss_mdp_set_intr_callback(MDSS_MDP_IRQ_PING_PONG_RD_PTR,
		ctx->pp_num, NULL, NULL);
	mdss_mdp_set_intr_callback(MDSS_MDP_IRQ_PING_PONG_COMP,
		ctx->pp_num, NULL, NULL);

	memset(ctx, 0, sizeof(*ctx));

	return 0;
}

int mdss_mdp_cmd_intfs_stop(struct mdss_mdp_ctl *ctl, int session,
	int panel_power_state, bool pend_switch)
{
	struct mdss_mdp_cmd_ctx *ctx;

	if (session >= MAX_SESSIONS)
		return 0;

	ctx = ctl->intf_ctx[MASTER_CTX];
	if (!ctx->ref_cnt) {
		pr_err("invalid ctx session: %d\n", session);
		return -ENODEV;
	}

	mdss_mdp_cmd_ctx_stop(ctl, ctx, panel_power_state, pend_switch);

	if (is_pingpong_split(ctl->mfd)) {
		session += 1;

		if (session >= MAX_SESSIONS)
			return 0;

		ctx = ctl->intf_ctx[SLAVE_CTX];
		if (!ctx->ref_cnt) {
			pr_err("invalid ctx session: %d\n", session);
			return -ENODEV;
		}
		mdss_mdp_cmd_ctx_stop(ctl, ctx, panel_power_state, pend_switch);
	}
	pr_debug("%s:-\n", __func__);
	return 0;
}

static int mdss_mdp_cmd_stop_sub(struct mdss_mdp_ctl *ctl,
		int panel_power_state, bool pend_switch)
{
	struct mdss_mdp_cmd_ctx *ctx;
	struct mdss_mdp_vsync_handler *tmp, *handle;
	int session;

	ctx = (struct mdss_mdp_cmd_ctx *) ctl->intf_ctx[MASTER_CTX];
	if (!ctx) {
		pr_err("invalid ctx\n");
		return -ENODEV;
	}

	list_for_each_entry_safe(handle, tmp, &ctx->vsync_handlers, list)
		mdss_mdp_cmd_remove_vsync_handler(ctl, handle);
	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt), XLOG_FUNC_ENTRY);

	/* Command mode is supported only starting at INTF1 */
	session = ctl->intf_num - MDSS_MDP_INTF1;
	return mdss_mdp_cmd_intfs_stop(ctl, session, panel_power_state,
			pend_switch);
}

int mdss_mdp_cmd_stop(struct mdss_mdp_ctl *ctl, int panel_power_state)
{
	struct mdss_mdp_cmd_ctx *ctx = ctl->intf_ctx[MASTER_CTX];
	struct mdss_mdp_ctl *sctl = mdss_mdp_get_split_ctl(ctl);
	bool panel_off = false;
	bool turn_off_clocks = false;
	bool send_panel_events = false;
	bool pend_switch = false;
	int ret = 0;

	if (!ctx) {
		pr_err("invalid ctx\n");
		return -ENODEV;
	}

	if (__mdss_mdp_cmd_is_panel_power_off(ctx)) {
		pr_debug("%s: panel already off\n", __func__);
		return 0;
	}

	if (ctx->panel_power_state == panel_power_state) {
		pr_debug("%s: no transition needed %d --> %d\n", __func__,
			ctx->panel_power_state, panel_power_state);
		return 0;
	}

	pr_debug("%s: transition from %d --> %d\n", __func__,
		ctx->panel_power_state, panel_power_state);

	if (ctl->cmd_autorefresh_en) {
		int pre_suspend = ctx->autorefresh_pending_frame_cnt;
		mdss_mdp_cmd_enable_cmd_autorefresh(ctl, 0);
		ctx->autorefresh_pending_frame_cnt = pre_suspend;
	}

	mutex_lock(&ctl->offlock);
	if (mdss_panel_is_power_off(panel_power_state)) {
		/* Transition to display off */
		send_panel_events = true;
		turn_off_clocks = true;
		panel_off = true;
	} else if (__mdss_mdp_cmd_is_panel_power_on_interactive(ctx)) {
		/*
		 * If we are transitioning from interactive to low
		 * power, then we need to send events to the interface
		 * so that the panel can be configured in low power
		 * mode.
		 */
		send_panel_events = true;
		if (mdss_panel_is_power_on_ulp(panel_power_state))
			turn_off_clocks = true;
	} else {
		/* Transitions between low power and ultra low power */
		if (mdss_panel_is_power_on_ulp(panel_power_state)) {
			/*
			 * If we are transitioning from low power to ultra low
			 * power mode, no more display updates are expected.
			 * Turn off the interface clocks.
			 */
			pr_debug("%s: turn off clocks\n", __func__);
			turn_off_clocks = true;
		} else {
			/*
			 * Transition from ultra low power to low power does
			 * not require any special handling. Just rest the
			 * intf_stopped flag so that the clocks would
			 * get turned on when the first update comes.
			 */
			pr_debug("%s: reset intf_stopped flag.\n", __func__);
			ctx->intf_stopped = 0;
			goto end;
		}
	}

	if (!turn_off_clocks)
		goto panel_events;

	if (ctx->pending_mode_switch) {
		pend_switch = true;
		send_panel_events = false;
		ctx->pending_mode_switch = 0;
	}

	pr_debug("%s: turn off interface clocks\n", __func__);
	ret = mdss_mdp_cmd_stop_sub(ctl, panel_power_state, pend_switch);
	if (IS_ERR_VALUE(ret)) {
		pr_err("%s: unable to stop interface: %d\n",
				__func__, ret);
		goto end;
	}

	if (sctl) {
		mdss_mdp_cmd_stop_sub(sctl, panel_power_state, pend_switch);
		if (IS_ERR_VALUE(ret)) {
			pr_err("%s: unable to stop slave intf: %d\n",
					__func__, ret);
			goto end;
		}
	}

panel_events:
	if ((!is_panel_split(ctl->mfd) || is_pingpong_split(ctl->mfd) ||
		(is_panel_split(ctl->mfd) && sctl)) && send_panel_events) {
		pr_debug("%s: send panel events\n", __func__);
		ret = mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_BLANK,
				(void *) (long int) panel_power_state, false);
		WARN(ret, "intf %d unblank error (%d)\n", ctl->intf_num, ret);

		ret = mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_PANEL_OFF,
				(void *) (long int) panel_power_state, false);
		WARN(ret, "intf %d unblank error (%d)\n", ctl->intf_num, ret);
	}


	if (!panel_off) {
		pr_debug("%s: cmd_stop with panel always on\n", __func__);
		goto end;
	}

	pr_debug("%s: turn off panel\n", __func__);
	ctl->intf_ctx[MASTER_CTX] = NULL;
	ctl->intf_ctx[SLAVE_CTX] = NULL;
	ctl->ops.stop_fnc = NULL;
	ctl->ops.display_fnc = NULL;
	ctl->ops.wait_pingpong = NULL;
	ctl->ops.add_vsync_handler = NULL;
	ctl->ops.remove_vsync_handler = NULL;

end:
	if (!IS_ERR_VALUE(ret))
		ctx->panel_power_state = panel_power_state;
	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt), XLOG_FUNC_EXIT);
	mutex_unlock(&ctl->offlock);
	pr_debug("%s:-\n", __func__);

	return ret;
}

static int mdss_mdp_cmd_ctx_setup(struct mdss_mdp_ctl *ctl,
	struct mdss_mdp_cmd_ctx *ctx, int pp_num,
	int pingpong_split_slave)
{
	int ret = 0;

	ctx->ctl = ctl;
	ctx->pp_num = pp_num;
	ctx->pingpong_split_slave = pingpong_split_slave;
	ctx->pp_timeout_report_cnt = 0;
	init_waitqueue_head(&ctx->pp_waitq);
	init_completion(&ctx->stop_comp);
	init_completion(&ctx->readptr_done);
	init_completion(&ctx->pp_done);
	spin_lock_init(&ctx->clk_lock);
	spin_lock_init(&ctx->koff_lock);
	mutex_init(&ctx->clk_mtx);
	mutex_init(&ctx->mdp_rdptr_lock);
	mutex_init(&ctx->autorefresh_mtx);
	INIT_WORK(&ctx->gate_clk_work, clk_ctrl_gate_work);
	INIT_DELAYED_WORK(&ctx->delayed_off_clk_work,
		clk_ctrl_delayed_off_work);
	INIT_WORK(&ctx->pp_done_work, pingpong_done_work);
	atomic_set(&ctx->pp_done_cnt, 0);
	ctx->autorefresh_off_pending = false;
	ctx->autorefresh_init = false;
	INIT_LIST_HEAD(&ctx->vsync_handlers);

	ctx->intf_recovery.fxn = mdss_mdp_cmd_intf_recovery;
	ctx->intf_recovery.data = ctx;

	ctx->intf_stopped = 0;

	pr_debug("%s: ctx=%p num=%d\n", __func__, ctx, ctx->pp_num);
	MDSS_XLOG(ctl->num, atomic_read(&ctx->koff_cnt));

	mdss_mdp_set_intr_callback(MDSS_MDP_IRQ_PING_PONG_RD_PTR,
		ctx->pp_num, mdss_mdp_cmd_readptr_done, ctl);

	mdss_mdp_set_intr_callback(MDSS_MDP_IRQ_PING_PONG_COMP, ctx->pp_num,
				   mdss_mdp_cmd_pingpong_done, ctl);

	ret = mdss_mdp_cmd_tearcheck_setup(ctx, true);
	if (ret)
		pr_err("tearcheck setup failed\n");

	return ret;
}

static int mdss_mdp_cmd_intfs_setup(struct mdss_mdp_ctl *ctl,
			int session)
{
	struct mdss_mdp_cmd_ctx *ctx;
	struct mdss_mdp_ctl *sctl = NULL;
	struct mdss_mdp_mixer *mixer;
	int ret;

	if (session >= MAX_SESSIONS)
		return 0;

	sctl = mdss_mdp_get_split_ctl(ctl);
	ctx = &mdss_mdp_cmd_ctx_list[session];
	if (ctx->ref_cnt) {
		if (mdss_panel_is_power_on(ctx->panel_power_state)) {
			pr_debug("%s: cmd_start with panel always on\n",
				__func__);
			/*
			 * It is possible that the resume was called from the
			 * panel always on state without MDSS every
			 * power-collapsed (such as a case with any other
			 * interfaces connected). In such cases, we need to
			 * explictly call the restore function to enable
			 * tearcheck logic.
			 */
			mdss_mdp_cmd_restore(ctl);

			/* Turn on panel so that it can exit low power mode */
			return mdss_mdp_cmd_panel_on(ctl, sctl);
		} else {
			pr_err("Intf %d already in use\n", session);
			return -EBUSY;
		}
	}
	ctx->ref_cnt++;

	mixer = mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_LEFT);
	if (!mixer) {
		pr_err("mixer not setup correctly\n");
		return -ENODEV;
	}

	ctl->intf_ctx[MASTER_CTX] = ctx;

	/*
	 * pp_num for master ctx is same as mixer num independent
	 * of pingpong split enabled/disabled
	 */
	ret = mdss_mdp_cmd_ctx_setup(ctl, ctx, mixer->num, false);
	if (ret) {
		pr_err("mdss_mdp_cmd_ctx_setup failed for ping ping: %d\n",
				mixer->num);
		ctx->ref_cnt--;
		return -ENODEV;
	}

	if (is_pingpong_split(ctl->mfd)) {
		session += 1;
		if (session >= MAX_SESSIONS)
			return 0;
		ctx = &mdss_mdp_cmd_ctx_list[session];
		if (ctx->ref_cnt) {
			if (mdss_panel_is_power_on(ctx->panel_power_state)) {
				pr_debug("%s: cmd_start with panel always on\n",
						__func__);
				mdss_mdp_cmd_restore(ctl);
				return mdss_mdp_cmd_panel_on(ctl, sctl);
			} else {
				pr_err("Intf %d already in use\n", session);
				return -EBUSY;
			}
		}
		ctx->ref_cnt++;

		ctl->intf_ctx[SLAVE_CTX] = ctx;

		ret = mdss_mdp_cmd_ctx_setup(ctl, ctx, session, true);
		if (ret) {
			pr_err("mdss_mdp_cmd_ctx_setup failed for slave ping pong block");
			ctx->ref_cnt--;
			return -EPERM;
		}
	}
	return 0;
}

void mdss_mdp_switch_roi_reset(struct mdss_mdp_ctl *ctl)
{
	struct mdss_mdp_ctl *sctl = mdss_mdp_get_split_ctl(ctl);

	if (!ctl->panel_data ||
	  !ctl->panel_data->panel_info.partial_update_supported)
		return;

	ctl->panel_data->panel_info.roi = ctl->roi;
	if (sctl && sctl->panel_data)
		sctl->panel_data->panel_info.roi = sctl->roi;

	mdss_mdp_cmd_set_partial_roi(ctl);
}

void mdss_mdp_switch_to_vid_mode(struct mdss_mdp_ctl *ctl, int prep)
{
	struct mdss_mdp_cmd_ctx *ctx = ctl->intf_ctx[MASTER_CTX];
	long int mode = MIPI_VIDEO_PANEL;
	int rc = 0;

	pr_debug("%s start, prep = %d\n", __func__, prep);

	if (prep) {
		/*
		 * In dsi_on there is an explicit decrement to dsi clk refcount
		 * if we are in cmd mode. We need to rebalance clock in order
		 * to properly enable vid mode compnents
		 */
		rc = mdss_mdp_ctl_intf_event
			(ctl, MDSS_EVENT_PANEL_CLK_CTRL, (void *)1, false);

		ctx->pending_mode_switch = 1;
		return;
	}

	mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_DSI_RECONFIG_CMD,
			(void *) mode, false);
}

int mdss_mdp_cmd_start(struct mdss_mdp_ctl *ctl)
{
	int ret, session = 0;

	pr_debug("%s:+\n", __func__);

	/* Command mode is supported only starting at INTF1 */
	session = ctl->intf_num - MDSS_MDP_INTF1;
	ret = mdss_mdp_cmd_intfs_setup(ctl, session);
	if (IS_ERR_VALUE(ret)) {
		pr_err("unable to set cmd interface: %d\n", ret);
		return ret;
	}

	ctl->ops.stop_fnc = mdss_mdp_cmd_stop;
	ctl->ops.display_fnc = mdss_mdp_cmd_kickoff;
	ctl->ops.wait_pingpong = mdss_mdp_cmd_wait4pingpong;
	ctl->ops.add_vsync_handler = mdss_mdp_cmd_add_vsync_handler;
	ctl->ops.remove_vsync_handler = mdss_mdp_cmd_remove_vsync_handler;
	ctl->ops.read_line_cnt_fnc = mdss_mdp_cmd_line_count;
	ctl->ops.restore_fnc = mdss_mdp_cmd_restore;
	pr_debug("%s:-\n", __func__);

	return 0;
}

