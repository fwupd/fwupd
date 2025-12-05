/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-device.h"

/* ---- bank ids ---- */

enum {
	/* system banks */
	PXI_TP_SYSTEM_BANK0 = 0x00,
	PXI_TP_SYSTEM_BANK1 = 0x01,
	PXI_TP_SYSTEM_BANK2 = 0x02,
	PXI_TP_SYSTEM_BANK4 = 0x04,
	PXI_TP_SYSTEM_BANK6 = 0x06,

	/* user banks */
	PXI_TP_USER_BANK0 = 0x00,
	PXI_TP_USER_BANK1 = 0x01,
	PXI_TP_USER_BANK2 = 0x02,
};

/* ---- System bank 4 (flash engine) ---- */

enum {
	PXI_TP_R_SYS4_FLASH_STATUS = 0x1c,
	PXI_TP_R_SYS4_SWAP_FLAG = 0x29,
	PXI_TP_R_SYS4_FLASH_INST_CMD = 0x2c,
	PXI_TP_R_SYS4_FLASH_BUF_ADDR0 = 0x2e,
	PXI_TP_R_SYS4_FLASH_BUF_ADDR1 = 0x2f,
	PXI_TP_R_SYS4_FLASH_CCR0 = 0x40,
	PXI_TP_R_SYS4_FLASH_CCR1 = 0x41,
	PXI_TP_R_SYS4_FLASH_CCR2 = 0x42,
	PXI_TP_R_SYS4_FLASH_CCR3 = 0x43,
	PXI_TP_R_SYS4_FLASH_DATACNT0 = 0x44,
	PXI_TP_R_SYS4_FLASH_DATACNT1 = 0x45,
	PXI_TP_R_SYS4_FLASH_ADDR0 = 0x48,
	PXI_TP_R_SYS4_FLASH_ADDR1 = 0x49,
	PXI_TP_R_SYS4_FLASH_ADDR2 = 0x4a,
	PXI_TP_R_SYS4_FLASH_ADDR3 = 0x4b,
	PXI_TP_R_SYS4_FLASH_EXECUTE = 0x56,
};

/* ---- System bank 6 (SRAM buffer) ---- */

enum {
	PXI_TP_R_SYS6_SRAM_GAIN_SELECT = 0x08,
	PXI_TP_R_SYS6_SRAM_SELECT = 0x09,
	PXI_TP_R_SYS6_SRAM_TRIGGER = 0x0a,
	PXI_TP_R_SYS6_SRAM_DATA = 0x0b,
	PXI_TP_R_SYS6_SRAM_CHKSUM = 0x0c,
	PXI_TP_R_SYS6_SRAM_ADDR0 = 0x10,
	PXI_TP_R_SYS6_SRAM_ADDR1 = 0x11,
};

/* ---- System bank 1 (reset control) ---- */

enum {
	PXI_TP_R_SYS1_RESET_KEY1 = 0x2c,
	PXI_TP_R_SYS1_RESET_KEY2 = 0x2d,
};

/* ---- System bank 2 (update mode) ---- */

enum {
	PXI_TP_R_SYS2_UPDATE_MODE = 0x0d,
};

/* ---- User bank 0 (part ID + CRC) ---- */

enum {
	PXI_TP_R_USER0_PROXY_MODE = 0x56,
	PXI_TP_R_USER0_PART_ID0 = 0x78,
	PXI_TP_R_USER0_PART_ID1 = 0x79,
	PXI_TP_R_USER0_CRC_CTRL = 0x82,
	PXI_TP_R_USER0_CRC_RESULT0 = 0x84,
	PXI_TP_R_USER0_CRC_RESULT1 = 0x85,
	PXI_TP_R_USER0_CRC_RESULT2 = 0x86,
	PXI_TP_R_USER0_CRC_RESULT3 = 0x87,
};

gboolean
fu_pxi_tp_register_write(FuPxiTpDevice *self, guint8 bank, guint8 addr, guint8 val, GError **error);

gboolean
fu_pxi_tp_register_read(FuPxiTpDevice *self,
			guint8 bank,
			guint8 addr,
			guint8 *out_val,
			GError **error);

gboolean
fu_pxi_tp_register_user_write(FuPxiTpDevice *self,
			      guint8 bank,
			      guint8 addr,
			      guint8 val,
			      GError **error);

gboolean
fu_pxi_tp_register_user_read(FuPxiTpDevice *self,
			     guint8 bank,
			     guint8 addr,
			     guint8 *out_val,
			     GError **error);

gboolean
fu_pxi_tp_register_burst_write(FuPxiTpDevice *self, const guint8 *buf, gsize bufsz, GError **error);

gboolean
fu_pxi_tp_register_burst_read(FuPxiTpDevice *self, guint8 *buf, gsize bufsz, GError **error);
