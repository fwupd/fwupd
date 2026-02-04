/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#include <fcntl.h>
#include <unistd.h>

/* plugin type */
#define FU_TYPE_NOVATEK_TS_PLUGIN (fu_novatek_ts_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuNovatekTsPlugin, fu_novatek_ts_plugin, FU, NOVATEK_TS_PLUGIN, FuPlugin)

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
