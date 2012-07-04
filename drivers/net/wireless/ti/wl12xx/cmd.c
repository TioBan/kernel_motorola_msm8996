/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Copyright (C) 2011 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"

#include "wl12xx.h"
#include "cmd.h"

int wl1271_cmd_ext_radio_parms(struct wl1271 *wl)
{
	struct wl1271_ext_radio_parms_cmd *ext_radio_parms;
	struct wl12xx_priv *priv = wl->priv;
	struct wl12xx_conf_rf *rf = &priv->conf.rf;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	ext_radio_parms = kzalloc(sizeof(*ext_radio_parms), GFP_KERNEL);
	if (!ext_radio_parms)
		return -ENOMEM;

	ext_radio_parms->test.id = TEST_CMD_INI_FILE_RF_EXTENDED_PARAM;

	memcpy(ext_radio_parms->tx_per_channel_power_compensation_2,
	       rf->tx_per_channel_power_compensation_2,
	       CONF_TX_PWR_COMPENSATION_LEN_2);
	memcpy(ext_radio_parms->tx_per_channel_power_compensation_5,
	       rf->tx_per_channel_power_compensation_5,
	       CONF_TX_PWR_COMPENSATION_LEN_5);

	wl1271_dump(DEBUG_CMD, "TEST_CMD_INI_FILE_EXT_RADIO_PARAM: ",
		    ext_radio_parms, sizeof(*ext_radio_parms));

	ret = wl1271_cmd_test(wl, ext_radio_parms, sizeof(*ext_radio_parms), 0);
	if (ret < 0)
		wl1271_warning("TEST_CMD_INI_FILE_RF_EXTENDED_PARAM failed");

	kfree(ext_radio_parms);
	return ret;
}

int wl1271_cmd_general_parms(struct wl1271 *wl)
{
	struct wl1271_general_parms_cmd *gen_parms;
	struct wl1271_ini_general_params *gp =
		&((struct wl1271_nvs_file *)wl->nvs)->general_params;
	bool answer = false;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	if (gp->tx_bip_fem_manufacturer >= WL1271_INI_FEM_MODULE_COUNT) {
		wl1271_warning("FEM index from INI out of bounds");
		return -EINVAL;
	}

	gen_parms = kzalloc(sizeof(*gen_parms), GFP_KERNEL);
	if (!gen_parms)
		return -ENOMEM;

	gen_parms->test.id = TEST_CMD_INI_FILE_GENERAL_PARAM;

	memcpy(&gen_parms->general_params, gp, sizeof(*gp));

	if (gp->tx_bip_fem_auto_detect)
		answer = true;

	/* Override the REF CLK from the NVS with the one from platform data */
	gen_parms->general_params.ref_clock = wl->ref_clock;

	ret = wl1271_cmd_test(wl, gen_parms, sizeof(*gen_parms), answer);
	if (ret < 0) {
		wl1271_warning("CMD_INI_FILE_GENERAL_PARAM failed");
		goto out;
	}

	gp->tx_bip_fem_manufacturer =
		gen_parms->general_params.tx_bip_fem_manufacturer;

	if (gp->tx_bip_fem_manufacturer >= WL1271_INI_FEM_MODULE_COUNT) {
		wl1271_warning("FEM index from FW out of bounds");
		ret = -EINVAL;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "FEM autodetect: %s, manufacturer: %d\n",
		     answer ? "auto" : "manual", gp->tx_bip_fem_manufacturer);

out:
	kfree(gen_parms);
	return ret;
}

int wl128x_cmd_general_parms(struct wl1271 *wl)
{
	struct wl128x_general_parms_cmd *gen_parms;
	struct wl128x_ini_general_params *gp =
		&((struct wl128x_nvs_file *)wl->nvs)->general_params;
	bool answer = false;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	if (gp->tx_bip_fem_manufacturer >= WL1271_INI_FEM_MODULE_COUNT) {
		wl1271_warning("FEM index from ini out of bounds");
		return -EINVAL;
	}

	gen_parms = kzalloc(sizeof(*gen_parms), GFP_KERNEL);
	if (!gen_parms)
		return -ENOMEM;

	gen_parms->test.id = TEST_CMD_INI_FILE_GENERAL_PARAM;

	memcpy(&gen_parms->general_params, gp, sizeof(*gp));

	if (gp->tx_bip_fem_auto_detect)
		answer = true;

	/* Replace REF and TCXO CLKs with the ones from platform data */
	gen_parms->general_params.ref_clock = wl->ref_clock;
	gen_parms->general_params.tcxo_ref_clock = wl->tcxo_clock;

	ret = wl1271_cmd_test(wl, gen_parms, sizeof(*gen_parms), answer);
	if (ret < 0) {
		wl1271_warning("CMD_INI_FILE_GENERAL_PARAM failed");
		goto out;
	}

	gp->tx_bip_fem_manufacturer =
		gen_parms->general_params.tx_bip_fem_manufacturer;

	if (gp->tx_bip_fem_manufacturer >= WL1271_INI_FEM_MODULE_COUNT) {
		wl1271_warning("FEM index from FW out of bounds");
		ret = -EINVAL;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "FEM autodetect: %s, manufacturer: %d\n",
		     answer ? "auto" : "manual", gp->tx_bip_fem_manufacturer);

out:
	kfree(gen_parms);
	return ret;
}

int wl1271_cmd_radio_parms(struct wl1271 *wl)
{
	struct wl1271_nvs_file *nvs = (struct wl1271_nvs_file *)wl->nvs;
	struct wl1271_radio_parms_cmd *radio_parms;
	struct wl1271_ini_general_params *gp = &nvs->general_params;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	radio_parms = kzalloc(sizeof(*radio_parms), GFP_KERNEL);
	if (!radio_parms)
		return -ENOMEM;

	radio_parms->test.id = TEST_CMD_INI_FILE_RADIO_PARAM;

	/* 2.4GHz parameters */
	memcpy(&radio_parms->static_params_2, &nvs->stat_radio_params_2,
	       sizeof(struct wl1271_ini_band_params_2));
	memcpy(&radio_parms->dyn_params_2,
	       &nvs->dyn_radio_params_2[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl1271_ini_fem_params_2));

	/* 5GHz parameters */
	memcpy(&radio_parms->static_params_5,
	       &nvs->stat_radio_params_5,
	       sizeof(struct wl1271_ini_band_params_5));
	memcpy(&radio_parms->dyn_params_5,
	       &nvs->dyn_radio_params_5[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl1271_ini_fem_params_5));

	wl1271_dump(DEBUG_CMD, "TEST_CMD_INI_FILE_RADIO_PARAM: ",
		    radio_parms, sizeof(*radio_parms));

	ret = wl1271_cmd_test(wl, radio_parms, sizeof(*radio_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_RADIO_PARAM failed");

	kfree(radio_parms);
	return ret;
}

int wl128x_cmd_radio_parms(struct wl1271 *wl)
{
	struct wl128x_nvs_file *nvs = (struct wl128x_nvs_file *)wl->nvs;
	struct wl128x_radio_parms_cmd *radio_parms;
	struct wl128x_ini_general_params *gp = &nvs->general_params;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	radio_parms = kzalloc(sizeof(*radio_parms), GFP_KERNEL);
	if (!radio_parms)
		return -ENOMEM;

	radio_parms->test.id = TEST_CMD_INI_FILE_RADIO_PARAM;

	/* 2.4GHz parameters */
	memcpy(&radio_parms->static_params_2, &nvs->stat_radio_params_2,
	       sizeof(struct wl128x_ini_band_params_2));
	memcpy(&radio_parms->dyn_params_2,
	       &nvs->dyn_radio_params_2[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl128x_ini_fem_params_2));

	/* 5GHz parameters */
	memcpy(&radio_parms->static_params_5,
	       &nvs->stat_radio_params_5,
	       sizeof(struct wl128x_ini_band_params_5));
	memcpy(&radio_parms->dyn_params_5,
	       &nvs->dyn_radio_params_5[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl128x_ini_fem_params_5));

	radio_parms->fem_vendor_and_options = nvs->fem_vendor_and_options;

	wl1271_dump(DEBUG_CMD, "TEST_CMD_INI_FILE_RADIO_PARAM: ",
		    radio_parms, sizeof(*radio_parms));

	ret = wl1271_cmd_test(wl, radio_parms, sizeof(*radio_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_RADIO_PARAM failed");

	kfree(radio_parms);
	return ret;
}
