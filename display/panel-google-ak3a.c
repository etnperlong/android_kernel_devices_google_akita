// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based ak3a AMOLED LCD panel driver.
 *
 * Copyright (c) 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "panel/panel-samsung-drv.h"

static const unsigned char pps_setting[] = {
	0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x09, 0x60,
	0x04, 0x38, 0x00, 0x30, 0x02, 0x1C, 0x02, 0x1C,
	0x02, 0x00, 0x02, 0x0E, 0x00, 0x20, 0x04, 0xA6,
	0x00, 0x07, 0x00, 0x0C, 0x02, 0x0B, 0x02, 0x1F,
	0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
	0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38,
	0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7B,
	0x7D, 0x7E, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
	0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA, 0x19, 0xF8,
	0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xF6,
	0x2B, 0x34, 0x2B, 0x74, 0x3B, 0x74, 0x6B, 0xF4,
	0x00
};

#define AK3A_WRCTRLD_DIMMING_BIT    0x08
#define AK3A_WRCTRLD_BCTRL_BIT      0x20
#define AK3A_WRCTRLD_HBM_BIT        0xC0
#define AK3A_WRCTRLD_LOCAL_HBM_BIT  0x10
#define LHBM_RGB_RATIO_SIZE 3

static const u8 test_key_on_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_off_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 freq_update[] = { 0xF7, 0x0F };
static const u8 new_gamma_ip_enable[] = { 0x68, 0x02 };
static const u32 lhbm_1300_1100_rgb_ratio[LHBM_RGB_RATIO_SIZE] = {
	922974324, 910436713, 898442180
};
static const u32 lhbm_990_1300_rgb_ratio[LHBM_RGB_RATIO_SIZE] = {
	1089563019, 1063348416, 1099934254
};
static const u32 lhbm_1208_1300_rgb_ratio[LHBM_RGB_RATIO_SIZE] = {
	1029306415, 1018581722, 1029205963
};
static const u32 lhbm_1280_1300_rgb_ratio[LHBM_RGB_RATIO_SIZE] = {
	1005012531, 1005714286, 1003953871
};
static const u32 lhbm_1250_1300_rgb_ratio[LHBM_RGB_RATIO_SIZE] = {
	1013985465, 1011108127, 1012870314
};
static const u32 lhbm_1270_1300_rgb_ratio[LHBM_RGB_RATIO_SIZE] = {
	1005722353, 1004545049, 1005266073
};

static const struct exynos_dsi_cmd ak3a_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD_SEQ_DELAY(120, 0x10), /* sleep in */
};
static DEFINE_EXYNOS_CMD_SET(ak3a_off);

static const struct exynos_dsi_cmd ak3a_lp_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x5A, 0x5A), //test_key_on_f0),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_EVT1), 0xB9, 0x30),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_EVT1), 0xF7, 0x0F), /* freq_update */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_EVT1), 0x68, 0x01), /* new_gamma_ip_bypass */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_EVT1), 0x68, 0x02), /* new_gamma_ip_enable */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0xA5, 0xA5), /* test_key_off_f0 */
};
static DEFINE_EXYNOS_CMD_SET(ak3a_lp);

static const struct exynos_dsi_cmd ak3a_lp_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
};

static const struct exynos_dsi_cmd ak3a_lp_low_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(17, 0x53, 0x25), /* AOD 10 nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON),
};

static const struct exynos_dsi_cmd ak3a_lp_high_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(17, 0x53, 0x24), /* AOD 50 nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON)
};

static const struct exynos_binned_lp ak3a_binned_lp[] = {
	BINNED_LP_MODE("off", 0, ak3a_lp_off_cmds),
	/* rising time = delay = 12, falling time = delay + width = 12 + 35 */
	BINNED_LP_MODE_TIMING("low", 80, ak3a_lp_low_cmds, 12, 12 + 35),
	BINNED_LP_MODE_TIMING("high", 2047, ak3a_lp_high_cmds, 12, 12 + 35)
};

static const struct exynos_dsi_cmd ak3a_init_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_EXIT_SLEEP_MODE),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON, 0x00),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5F),
};
static DEFINE_EXYNOS_CMD_SET(ak3a_init);

static const struct exynos_dsi_cmd ak3a_lhbm_location_cmds[] = {
	/* test_key_on_f0 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x5A, 0x5A),
	/* test_key_on_f1 */
	EXYNOS_DSI_CMD_SEQ(0xF1, 0x5A, 0x5A),

	/* global para */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x07, 0x6D),
	/* box location */
	EXYNOS_DSI_CMD_SEQ(0x6D, 0xCC),
	/* global para */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x08, 0x6D),
	/* center position set, x: 0x21C, y: 0x6B1, size: 0x63 */
	EXYNOS_DSI_CMD_SEQ(0x6D, 0x21, 0xC6, 0xB1, 0x63),

	/* test_key_off_f1 */
	EXYNOS_DSI_CMD_SEQ(0xF1, 0xA5, 0xA5),
	/* test_key_off_f0 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0xA5, 0xA5)
};
static DEFINE_EXYNOS_CMD_SET(ak3a_lhbm_location);

static const struct exynos_dsi_cmd ak3a_mode_ns_60_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x5A, 0x5A), /* test_key_on_f0 */
	EXYNOS_DSI_CMD_SEQ(0x60, 0x18, 0x00), /* 60 hz NS */
	EXYNOS_DSI_CMD_SEQ(0xF7, 0x0F), /* freq_update */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0xA5, 0xA5), /* test_key_off_f0 */
};
static DEFINE_EXYNOS_CMD_SET(ak3a_mode_ns_60);

static const struct exynos_dsi_cmd ak3a_mode_hs_60_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x5A, 0x5A), /* test_key_on_f0 */
	EXYNOS_DSI_CMD_SEQ(0x60, 0x08, 0x00), /* 60 hz HS */
	EXYNOS_DSI_CMD_SEQ(0xF7, 0x0F), /* freq_update */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0xA5, 0xA5), /* test_key_off_f0 */
};
static DEFINE_EXYNOS_CMD_SET(ak3a_mode_hs_60);

static const struct exynos_dsi_cmd ak3a_mode_hs_90_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x5A, 0x5A), /* test_key_on_f0 */
	EXYNOS_DSI_CMD_SEQ(0x60, 0x00, 0x00), /* 90 hz HS */
	EXYNOS_DSI_CMD_SEQ(0xF7, 0x0F), /* freq_update */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0xA5, 0xA5), /* test_key_off_f0 */
};
static DEFINE_EXYNOS_CMD_SET(ak3a_mode_hs_90);

#define LHBM_GAMMA_CMD_SIZE 6
/**
 * struct ak3a_panel - panel specific runtime info
 *
 * This struct maintains ak3a panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc
 */
struct ak3a_panel {
	/** @base: base panel struct */
	struct exynos_panel base;

	/** @local_hbm_gamma: lhbm gamma data */
	struct local_hbm_gamma {
		u8 hs_cmd[LHBM_GAMMA_CMD_SIZE];
		u8 ns_cmd[LHBM_GAMMA_CMD_SIZE];
		u8 aod_cmd[LHBM_GAMMA_CMD_SIZE];
	} local_hbm_gamma;
};

#define to_spanel(ctx) container_of(ctx, struct ak3a_panel, base)

static void ak3a_update_lhbm_gamma(struct exynos_panel *ctx)
{
	/* ratio provided by HW for update the LHBM gamma.
	 * ratio must be a integer due to kernel didn't support floating.
	 * ratio original value R: 0.922974324, G: 0.910436713, B: 0.898442180.
	 * ratio cannot exceed u32 max 4294967296.
	 * R gamma hex from last 16bit from gamma_cmd[1] combine with gamma_cmd[3]
	 * G gamma hex from first 16bit from gamma_cmd[2] combine with gamma_cmd[4]
	 * B gamma hex from last 16bit from gamma_cmd[2] combine with gamma_cmd[5]
	 */
	struct ak3a_panel *spanel = to_spanel(ctx);
	u8 *gamma_cmd = spanel->local_hbm_gamma.hs_cmd;
	const int *rgb_ratio = NULL;
	const u8 rgb_offset[3][2] = {{1, 3}, {2, 4}, {2, 5}};
	u8 new_gamma_cmd[LHBM_GAMMA_CMD_SIZE] = {0};
	u64 tmp;
	int i;
	u16 mask, shift;

	if (ctx->panel_rev < PANEL_REV_EVT1)
		rgb_ratio = lhbm_1300_1100_rgb_ratio;
	else if (ctx->panel_rev == PANEL_REV_EVT1)
		rgb_ratio = lhbm_990_1300_rgb_ratio;
	else if (ctx->panel_rev == PANEL_REV_EVT1_0_2)
		rgb_ratio = lhbm_1208_1300_rgb_ratio;
	else if (ctx->panel_rev == PANEL_REV_EVT1_1)
		rgb_ratio = lhbm_1280_1300_rgb_ratio;
	else if (ctx->panel_rev == PANEL_REV_DVT1)
		rgb_ratio = lhbm_1250_1300_rgb_ratio;
	else if (ctx->panel_rev >= PANEL_REV_DVT1_1)
		rgb_ratio = lhbm_1270_1300_rgb_ratio;

	if (!rgb_ratio)
		return;

	dev_info(ctx->dev, "%s: gamma_cmd(%02x %02x %02x %02x %02x)\n", __func__,
		gamma_cmd[1], gamma_cmd[2], gamma_cmd[3], gamma_cmd[4], gamma_cmd[5]);
	for (i = 0; i < LHBM_RGB_RATIO_SIZE ; i++) {
		if (i % 2) {
			mask = 0xf0;
			shift = 4;
		} else {
			mask = 0x0f;
			shift = 0;
		}
		tmp = ((gamma_cmd[rgb_offset[i][0]] & mask) >> shift) << 8
			| gamma_cmd[rgb_offset[i][1]];
		dev_dbg(ctx->dev, "%s: lhbm_gamma[%d] = %llu\n", __func__, i, tmp);
		/* Round off and revert to original gamma value */
		tmp = (tmp * rgb_ratio[i] + 500000000)/1000000000;
		dev_dbg(ctx->dev, "%s: new lhbm_gamma[%d] = %llu\n", __func__, i, tmp);
		new_gamma_cmd[rgb_offset[i][0]] |= ((tmp & 0xff00) >> 8) << shift;
		new_gamma_cmd[rgb_offset[i][1]] |= tmp & 0xff;
	}
	memcpy(&gamma_cmd[1], &new_gamma_cmd[1], LHBM_GAMMA_CMD_SIZE - 1);
	dev_info(ctx->dev, "%s: new_gamma_cmd(%02x %02x %02x %02x %02x)\n", __func__,
		gamma_cmd[1], gamma_cmd[2], gamma_cmd[3], gamma_cmd[4], gamma_cmd[5]);
	dev_info(ctx->dev, "%s: rgb_ratio(%u %u %u)\n", __func__,
		rgb_ratio[0], rgb_ratio[1], rgb_ratio[2]);
}

static void ak3a_lhbm_gamma_read(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct ak3a_panel *spanel = to_spanel(ctx);
	int ret;
	u8 *hs_cmd = spanel->local_hbm_gamma.hs_cmd;
	u8 *ns_cmd = spanel->local_hbm_gamma.ns_cmd;
	u8 *aod_cmd = spanel->local_hbm_gamma.aod_cmd;
	u8 buf[LHBM_GAMMA_CMD_SIZE * 2] = {0};

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xB0, 0x00, 0x22, 0xD8); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xD8, hs_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1);
	if (ret == (LHBM_GAMMA_CMD_SIZE - 1)) {
		/* fill in gamma write command 0x66 in offset 0 */
		hs_cmd[0] = 0x66;
		exynos_bin2hex(hs_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1,
			buf, sizeof(buf));
		dev_info(ctx->dev, "%s: hs_gamma: %s\n", __func__, buf);
	} else {
		dev_err(ctx->dev, "fail to read LHBM gamma for HS\n");
	}
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x1D, 0xD8); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xD8, ns_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1);
	if (ret == (LHBM_GAMMA_CMD_SIZE - 1)) {
		/* fill in gamma write command 0x66 in offset 0 */
		ns_cmd[0] = 0x66;
		exynos_bin2hex(ns_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1,
			buf, sizeof(buf));
		dev_info(ctx->dev, "%s: ns_gamma: %s\n", __func__, buf);
	} else {
		dev_err(ctx->dev, "fail to read LHBM gamma for NS\n");
	}

	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x18, 0xD8); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xD8, aod_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1);
	if (ret == (LHBM_GAMMA_CMD_SIZE - 1)) {
		/* fill in gamma write command 0x66 in offset 0 */
		aod_cmd[0] = 0x66;
		exynos_bin2hex(aod_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1,
			buf, sizeof(buf));
		dev_info(ctx->dev, "%s: aod_gamma: %s\n", __func__, buf);
	} else {
		dev_err(ctx->dev, "fail to read LHBM gamma for AOD\n");
	}

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	ak3a_update_lhbm_gamma(ctx);
}

static void ak3a_lhbm_gamma_write(struct exynos_panel *ctx)
{
	struct ak3a_panel *spanel = to_spanel(ctx);
	const u8 *hs_cmd = spanel->local_hbm_gamma.hs_cmd;
	const u8 *ns_cmd = spanel->local_hbm_gamma.ns_cmd;
	const u8 *aod_cmd = spanel->local_hbm_gamma.aod_cmd;

	if (!hs_cmd[0] && !ns_cmd[0] && !aod_cmd[0]) {
		dev_err(ctx->dev, "%s: no lhbm gamma!\n", __func__);
		return;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_on_f0);

	if (hs_cmd[0]) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x03, 0xD7, 0x66); /* global para */
		exynos_dcs_write(ctx, hs_cmd, LHBM_GAMMA_CMD_SIZE); /* write gamma */
	}
	if (ns_cmd[0]) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x03, 0xE6, 0x66); /* global para */
		exynos_dcs_write(ctx, ns_cmd, LHBM_GAMMA_CMD_SIZE); /* write gamma */
	}
	if (aod_cmd[0]) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x03, 0xEB, 0x66); /* global para */
		exynos_dcs_write(ctx, aod_cmd, LHBM_GAMMA_CMD_SIZE); /* write gamma */
	}

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void ak3a_get_te2_setting(struct exynos_panel_te2_timing *timing,
				    u8 *setting)
{
	u8 delay_low_byte, delay_high_byte;
	u8 width_low_byte, width_high_byte;
	u32 rising, falling;

	if (!timing || !setting)
		return;

	rising = timing->rising_edge;
	falling = timing->falling_edge;

	delay_low_byte = rising & 0xFF;
	delay_high_byte = (rising >> 8) & 0xF;
	width_low_byte = (falling - rising) & 0xFF;
	width_high_byte = ((falling - rising) >> 8) & 0xF;

	setting[0] = (delay_high_byte << 4) | width_high_byte;
	setting[1] = delay_low_byte;
	setting[2] = width_low_byte;
}

static void ak3a_update_te2(struct exynos_panel *ctx)
{
	struct exynos_panel_te2_timing timing;
	u8 hs_90hz_setting[4] = {0xCB, 0x00, 0x0C, 0x32};
	u8 hs_60hz_setting[4] = {0xCB, 0x00, 0x0C, 0x32};

	u8 lp_setting[4] = {0xCB, 0x00, 0x0C, 0x23}; // lp low/high

	if (!ctx)
		return;

	/* HS mode */
	timing = ctx->te2.mode_data[0].timing;
	ak3a_get_te2_setting(&timing, &hs_90hz_setting[1]);

	dev_dbg(ctx->dev, "TE2 updated HS 90Hz: [HEX] %*ph\n", 4, hs_90hz_setting);

	timing = ctx->te2.mode_data[1].timing;
	ak3a_get_te2_setting(&timing, &hs_60hz_setting[1]);

	dev_dbg(ctx->dev, "TE2 updated HS 60Hz: [HEX] %*ph\n", 4, hs_60hz_setting);

	/* LP mode */
	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		int ret = exynos_panel_get_current_mode_te2(ctx, &timing);

		if (!ret)
			ak3a_get_te2_setting(&timing, &lp_setting[1]);
		else if (ret == -EAGAIN)
			dev_dbg(ctx->dev,
				"Panel is not ready, use default setting\n");
		else
			return;

		dev_dbg(ctx->dev, "TE2 updated LP: [HEX] %*ph\n", 4, lp_setting);
	}

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x2B, 0xF2); /* global para */
	EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x03, 0x14); /* TE2 on */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x73, 0xCB); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, hs_90hz_setting); /* HS 90Hz control */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xC9, 0xCB); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, hs_60hz_setting); /* HS 60Hz control */
	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x01, 0xC8, 0xCB); /* global para */
		EXYNOS_DCS_BUF_ADD_SET(ctx, lp_setting); /* HLPM mode */
	}
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update); /* LTPS update */
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void ak3a_change_frequency(struct exynos_panel *ctx,
				    const unsigned int vrefresh)
{
	const u8 hs60_setting[3] = {0x60, 0x08, 0x00}; // High Speed 60Hz
	const u8 hs90_setting[3] = {0x60, 0x00, 0x00}; // High Speed 90Hz

	if (!ctx || (vrefresh != 60 && vrefresh != 90))
		return;

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	if (vrefresh == 90) {
		EXYNOS_DCS_BUF_ADD_SET(ctx, hs90_setting);
		if (ctx->panel_rev >= PANEL_REV_EVT1) {
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x31);
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x10, 0xB9);
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x00, 0x25, 0x00, 0x0C);
		}
	} else {
		EXYNOS_DCS_BUF_ADD_SET(ctx, hs60_setting);
		if (ctx->panel_rev >= PANEL_REV_EVT1)
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x30);
	}
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	dev_dbg(ctx->dev, "%s: change to %uhz\n", __func__, vrefresh);
}

static int ak3a_set_op_hz(struct exynos_panel *ctx, unsigned int hz)
{
	const unsigned int vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if ((vrefresh > hz) || ((hz != 60) && (hz != 90))) {
		dev_err(ctx->dev, "invalid op_hz=%u for vrefresh=%u\n",
			hz, vrefresh);
		return -EINVAL;
	}

	ctx->op_hz = hz;
	if (ctx->op_hz == 60) {
		exynos_panel_send_cmd_set(ctx,
			&ak3a_mode_ns_60_cmd_set);
	} else {
		if (vrefresh == 60) {
			exynos_panel_send_cmd_set(ctx,
				&ak3a_mode_hs_60_cmd_set);
		} else {
			exynos_panel_send_cmd_set(ctx,
				&ak3a_mode_hs_90_cmd_set);
		}
	}
	dev_info(ctx->dev, "set op_hz at %u\n", hz);
	return 0;
}

static void ak3a_update_wrctrld(struct exynos_panel *ctx)
{
	u8 val = AK3A_WRCTRLD_BCTRL_BIT;

	if (IS_HBM_ON(ctx->hbm_mode))
		val |= AK3A_WRCTRLD_HBM_BIT;

	if (ctx->hbm.local_hbm.enabled)
		val |= AK3A_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= AK3A_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev,
		"%s(wrctrld:0x%x, hbm: %s, dimming: %s, local_hbm: %s)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		ctx->dimming_on ? "on" : "off",
		ctx->hbm.local_hbm.enabled ? "on" : "off");

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

#define MAX_BR_HBM_EVT1_0_2 4094
static int ak3a_set_brightness(struct exynos_panel *ctx, u16 br)
{
	u16 brightness;

	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	if (ctx->panel_rev <= PANEL_REV_EVT1_0_2 && br >= MAX_BR_HBM_EVT1_0_2) {
		br = MAX_BR_HBM_EVT1_0_2;
		dev_dbg(ctx->dev, "%s: capped to dbv(%d) for EVT1_0_2 and before\n",
			__func__, MAX_BR_HBM_EVT1_0_2);
	}

	brightness = (br & 0xff) << 8 | br >> 8;

	return exynos_dcs_set_brightness(ctx, brightness);
}

static void ak3a_set_nolp_mode(struct exynos_panel *ctx,
				  const struct exynos_panel_mode *pmode)
{
	unsigned int vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 delay_us = mult_frac(1000, 1020, vrefresh);

	if (!ctx->enabled)
		return;

	EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, new_gamma_ip_enable);
	/* backlight control and dimming */
	ak3a_update_wrctrld(ctx);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
	ak3a_change_frequency(ctx, vrefresh);
	usleep_range(delay_us, delay_us + 10);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_DISPLAY_ON);

	dev_info(ctx->dev, "exit LP mode\n");
}

static int ak3a_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;

	dev_dbg(ctx->dev, "%s\n", __func__);

	exynos_panel_reset(ctx);

	exynos_panel_send_cmd_set(ctx, &ak3a_init_cmd_set);

	ak3a_change_frequency(ctx, drm_mode_vrefresh(mode));

	ak3a_lhbm_gamma_write(ctx);
	exynos_panel_send_cmd_set(ctx, &ak3a_lhbm_location_cmd_set);

	/* DSC related configuration */
	exynos_dcs_compression_mode(ctx, 0x1); /* DSC_DEC_ON */
	EXYNOS_PPS_LONG_WRITE(ctx); /* PPS_SETTING */
	EXYNOS_DCS_BUF_ADD(ctx, 0xC2, 0x14); /* PPS_MIC_OFF */
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0x9D, 0x01); /* PPS_DSC_EN */

	ak3a_update_wrctrld(ctx); /* dimming and HBM */

	ctx->enabled = true;

	if (pmode->exynos_mode.is_lp_mode)
		exynos_panel_set_lp_mode(ctx, pmode);
	else
		EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON); /* display on */

	return 0;
}

static void ak3a_set_hbm_mode(struct exynos_panel *exynos_panel,
				enum exynos_hbm_mode mode)
{
	const bool hbm_update =
		(IS_HBM_ON(exynos_panel->hbm_mode) != IS_HBM_ON(mode));
	const bool irc_update =
		(IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode) != IS_HBM_ON_IRC_OFF(mode));

	exynos_panel->hbm_mode = mode;

	if (hbm_update)
		ak3a_update_wrctrld(exynos_panel);

	if (irc_update) {
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0xF0, 0x5A, 0x5A);
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0xF0, 0x5A, 0x5A);
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0xB0, 0x00, 0x01, 0x6A);
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0x6A, IS_HBM_ON_IRC_OFF(mode) ? 0x01 : 0x21);
		EXYNOS_DCS_BUF_ADD_AND_FLUSH(exynos_panel, 0xF0, 0xA5, 0xA5);
	}
	dev_info(exynos_panel->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(exynos_panel->hbm_mode),
		 IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode));
}

static void ak3a_set_dimming_on(struct exynos_panel *exynos_panel,
				 bool dimming_on)
{
	const struct exynos_panel_mode *pmode = exynos_panel->current_mode;

	exynos_panel->dimming_on = dimming_on;
	if (pmode->exynos_mode.is_lp_mode) {
		dev_info(exynos_panel->dev, "in lp mode, skip to update");
		return;
	}

	ak3a_update_wrctrld(exynos_panel);
}

static void ak3a_set_local_hbm_mode(struct exynos_panel *exynos_panel,
				 bool local_hbm_en)
{
	ak3a_update_wrctrld(exynos_panel);
}

static void ak3a_mode_set(struct exynos_panel *ctx,
			     const struct exynos_panel_mode *pmode)
{
	ak3a_change_frequency(ctx, drm_mode_vrefresh(&pmode->mode));
}

static bool ak3a_is_mode_seamless(const struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	/* seamless mode switch is possible if only changing refresh rate */
	return drm_mode_equal_no_clocks(&ctx->current_mode->mode, &pmode->mode);
}

static void ak3a_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	struct dentry *panel_root, *csroot;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot) {
		goto panel_out;
	}

	exynos_panel_debugfs_create_cmdset(ctx, csroot, &ak3a_init_cmd_set, "init");
	dput(csroot);
panel_out:
	dput(panel_root);
}

static void ak3a_panel_init(struct exynos_panel *ctx)
{
	ak3a_lhbm_gamma_read(ctx);
	ak3a_lhbm_gamma_write(ctx);
}

static void ak3a_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = build_code;

	switch (rev) {
	case 0x0A:
		ctx->panel_rev = PANEL_REV_PROTO1;
		break;
	case 0x14:
		ctx->panel_rev = PANEL_REV_PROTO1_1;
		break;
	case 0x18:
		ctx->panel_rev = PANEL_REV_PROTO1_2;
		break;
	case 0x20:
		ctx->panel_rev = PANEL_REV_EVT1;
		break;
	case 0x21:
		ctx->panel_rev = PANEL_REV_EVT1_0_2;
		break;
	case 0x24:
		ctx->panel_rev = PANEL_REV_EVT1_1;
		break;
	case 0x28:
		ctx->panel_rev = PANEL_REV_EVT1_2;
		break;
	case 0x40:
		ctx->panel_rev = PANEL_REV_DVT1;
		break;
	case 0x60:
		ctx->panel_rev = PANEL_REV_DVT1_1;
		break;
	case 0x80:
		ctx->panel_rev = PANEL_REV_PVT;
		break;
	default:
		dev_warn(ctx->dev,
			 "unknown rev from panel (0x%x), default to latest\n",
			 rev);
		ctx->panel_rev = PANEL_REV_LATEST;
		return;
	}

	dev_info(ctx->dev, "panel_rev: 0x%x, build id: 0x%x\n", ctx->panel_rev, rev);
}

static int ak3a_panel_probe(struct mipi_dsi_device *dsi)
{
	struct ak3a_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 90;

	return exynos_panel_common_init(dsi, &spanel->base);
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 1000,
	.te_var = 1,
};

static const u32 ak3a_bl_range[] = {
	95, 205, 315, 400, 2047
};

static const struct exynos_panel_mode ak3a_modes[] = {
	{
		.mode = {
			.name = "1080x2400x60",
			.clock = 168498,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 149,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 5720,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 48,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 12,
			.falling_edge = 12 + 50,
		},
	},
	{
		.mode = {
			.name = "1080x2400x90",
			.clock = 252747,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 149,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 222,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 48,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 12,
			.falling_edge = 12 + 50,
		},
	},
};

static const struct exynos_panel_mode ak3a_lp_mode = {
	.mode = {
		.name = "1080x2400x30",
		.clock = 84249,
		.hdisplay = 1080,
		.hsync_start = 1080 + 32, // add hfp
		.hsync_end = 1080 + 32 + 12, // add hsa
		.htotal = 1080 + 32 + 12 + 26, // add hbp
		.vdisplay = 2400,
		.vsync_start = 2400 + 12, // add vfp
		.vsync_end = 2400 + 12 + 4, // add vsa
		.vtotal = 2400 + 12 + 4 + 26, // add vbp
		.flags = 0,
		.type = DRM_MODE_TYPE_DRIVER,
		.width_mm = 70,
		.height_mm = 149,
	},
	.exynos_mode = {
		.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
		.vblank_usec = 120,
		.bpc = 8,
		.dsc = {
			.enabled = true,
			.dsc_count = 2,
			.slice_count = 2,
			.slice_height = 48,
		},
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static const struct drm_panel_funcs ak3a_drm_funcs = {
	.disable = exynos_panel_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = ak3a_enable,
	.get_modes = exynos_panel_get_modes,
	.debugfs_init = ak3a_debugfs_init,
};

static const struct exynos_panel_funcs ak3a_exynos_funcs = {
	.set_brightness = ak3a_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = ak3a_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = ak3a_set_hbm_mode,
	.set_dimming_on = ak3a_set_dimming_on,
	.set_local_hbm_mode = ak3a_set_local_hbm_mode,
	.is_mode_seamless = ak3a_is_mode_seamless,
	.mode_set = ak3a_mode_set,
	.panel_init = ak3a_panel_init,
	.get_panel_rev = ak3a_get_panel_rev,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = ak3a_update_te2,
	.set_op_hz = ak3a_set_op_hz,
	.read_id = exynos_panel_read_ddic_id,
};

const struct brightness_capability ak3a_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 600,
		},
		.level = {
			.min = 4,
			.max = 2047,
		},
		.percentage = {
			.min = 0,
			.max = 60,
		},
	},
	.hbm = {
		.nits = {
			.min = 600,
			.max = 1000,
		},
		.level = {
			.min = 2048,
			.max = 4095,
		},
		.percentage = {
			.min = 60,
			.max = 100,
		},
	},
};

const struct exynos_panel_desc google_ak3a = {
	.dsc_pps = pps_setting,
	.dsc_pps_len = ARRAY_SIZE(pps_setting),
	.data_lane_cnt = 4,
	.max_brightness = 4095,
	.min_brightness = 4,
	.dft_brightness = 1023,
	.brt_capability = &ak3a_brightness_capability,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.keep_atc_on_for_op = true,
	.bl_range = ak3a_bl_range,
	.bl_num_ranges = ARRAY_SIZE(ak3a_bl_range),
	.modes = ak3a_modes,
	.num_modes = ARRAY_SIZE(ak3a_modes),
	.off_cmd_set = &ak3a_off_cmd_set,
	.lp_mode = &ak3a_lp_mode,
	.lp_cmd_set = &ak3a_lp_cmd_set,
	.binned_lp = ak3a_binned_lp,
	.num_binned_lp = ARRAY_SIZE(ak3a_binned_lp),
	.panel_func = &ak3a_drm_funcs,
	.exynos_panel_func = &ak3a_exynos_funcs,
};

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,ak3a", .data = &google_ak3a },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = ak3a_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-ak3a",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Safayat Ullah <safayat@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google ak3a panel driver");
MODULE_LICENSE("GPL");
