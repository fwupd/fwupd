/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#define NVT_TS_PLUGIN_VERSION "3.0.1"

/* plugin type */
#define FU_TYPE_NVT_TS_PLUGIN (fu_nvt_ts_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuNvtTsPlugin, fu_nvt_ts_plugin, FU, NVT_TS_PLUGIN, FuPlugin)

#define NVT_TS_REPORT_ID 0x0B

#define NVT_VID_NUM		 0x0603
#define FLASH_PAGE_SIZE		 256
#define NVT_TRANSFER_LEN	 256
#define SIZE_4KB		 (1024 * 4)
#define SIZE_320KB		 (1024 * 320)
#define BYTE_PER_POINT		 2
#define FLASH_SECTOR_SIZE	 SIZE_4KB
#define MAX_BIN_SIZE		 SIZE_320KB
#define FW_BIN_END_FLAG_STR	 "NVT"
#define FW_BIN_END_FLAG_LEN	 3

/* Prefix existing error with context or set a new one, as a single statement. */
/* ex: [Update Normal FW Failed] <- [erase failed] <- ... <- [hid_write failed] */
#define SET_ERROR_OR_PREFIX(error, code, fmt, ...)                                                 \
	G_STMT_START                                                                               \
	{                                                                                          \
		g_autofree gchar *msg = g_strdup_printf(fmt, ##__VA_ARGS__);                       \
		if (*(error) != NULL)                                                              \
			g_prefix_error(error, "[%s] <- ", msg);                                    \
		else                                                                               \
			g_set_error(error, FWUPD_ERROR, code, "[%s]", msg);                        \
	}                                                                                          \
	G_STMT_END

typedef struct {
	uint32_t chip_ver_trim_addr;
	uint32_t swrst_sif_addr;
	uint32_t event_buf_cmd_addr;
	uint32_t event_buf_hs_sub_cmd_addr;
	uint32_t event_buf_reset_state_addr;
	uint32_t event_map_fwinfo_addr;
	uint32_t read_flash_checksum_addr;
	uint32_t rw_flash_data_addr;
	uint32_t enb_casc_addr;
	uint32_t hid_i2c_eng_addr;
	uint32_t gcm_code_addr;
	uint32_t gcm_flag_addr;
	uint32_t flash_cmd_addr;
	uint32_t flash_cmd_issue_addr;
	uint32_t flash_cksum_status_addr;
	uint32_t bld_spe_pups_addr;
	uint32_t q_wr_cmd_addr;
} FuNvtTsMemMap;

typedef struct {
	uint32_t flash_normal_fw_start_addr;
	uint32_t flash_pid_addr;
	/* max size starting at flash_normal_fw_start_addr */
	uint32_t flash_max_size;
} FuNvtTsFlashMap;

typedef struct {
	uint8_t flash_cmd;
	uint32_t flash_addr;
	uint16_t flash_checksum;
	uint8_t flash_addr_len;
	uint8_t pem_byte_len;
	uint8_t dummy_byte_len;
	uint8_t *tx_buf;
	uint16_t tx_len;
	uint8_t *rx_buf;
	uint16_t rx_len;
} FuNvtTsGcmXfer;

typedef enum {
	RESET_STATE_INIT = 0xA0,
	RESET_STATE_REK_BASELINE,
	RESET_STATE_REK_FINISH,
	RESET_STATE_NORMAL_RUN,
	RESET_STATE_MAX = 0xAF
} FuNvtTsResetState;

typedef struct {
	uint8_t *bin_data;
	uint32_t bin_size;
	uint32_t flash_start_addr;
} FuNvtTsFwBin;
