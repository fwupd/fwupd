/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-register.h"
#include "fu-pxi-tp-struct.h"
#include "fu-pxi-tp-tf-communication.h"

struct _FuPxiTpDevice {
	FuHidrawDevice parent_instance;
	guint8 sram_select;
	guint8 ver_bank;
	guint16 ver_addr;
};

G_DEFINE_TYPE(FuPxiTpDevice, fu_pxi_tp_device, FU_TYPE_HIDRAW_DEVICE)

/* ---- reset mode & keys ---- */

typedef enum {
	PXI_TP_RESET_MODE_APPLICATION = 0,
	PXI_TP_RESET_MODE_BOOTLOADER = 1,
} FuPxiTpResetMode;

enum {
	PXI_TP_RESET_KEY1_MAGIC = 0xaa,
	PXI_TP_RESET_KEY2_REGULAR = 0xbb,
	PXI_TP_RESET_KEY2_BOOTLOADER = 0xcc,
};

/* ---- flash command constants ---- */

enum {
	PXI_TP_FLASH_INST_CMD0 = 0x00,
	PXI_TP_FLASH_INST_CMD1 = 0x01,
};

enum {
	PXI_TP_FLASH_EXEC_STATE_BUSY = 0x01,
	PXI_TP_FLASH_EXEC_STATE_SUCCESS = 0x00,
	PXI_TP_FLASH_WRITE_ENABLE_SUCCESS = 0x02,
	PXI_TP_FLASH_STATUS_BUSY = 0x01, /* busy bit in status register */
};

enum {
	PXI_TP_FLASH_CCR_WRITE_ENABLE = 0x00000106,
	PXI_TP_FLASH_CCR_READ_STATUS = 0x01000105,
	PXI_TP_FLASH_CCR_ERASE_SECTOR = 0x00002520,
	PXI_TP_FLASH_CCR_PROGRAM_PAGE = 0x01002502,
};

/* ---- flash properties ---- */
enum {
	PXI_TP_SECTOR_SIZE = 4096,
	PXI_TP_PAGE_SIZE = 256,
	PXI_TP_PAGES_COUNT_PER_SECTOR = 16,
};

/* ---- device part-id values ---- */
enum {
	PXI_TP_PART_ID_PJP274 = 0x0274,
};

/* ---- CRC control for firmware ---- */
enum {
	/* CRC_CTRL mode: which firmware bank to calculate */
	PXI_TP_CRC_CTRL_FW_BANK0 = 0x02, /* firmware CRC on bank0 */
	PXI_TP_CRC_CTRL_FW_BANK1 = 0x10, /* firmware CRC on bank1 */

	/* CRC_CTRL mode: which parameter bank to calculate */
	PXI_TP_CRC_CTRL_PARAM_BANK0 = 0x04, /* parameter CRC on bank0 */
	PXI_TP_CRC_CTRL_PARAM_BANK1 = 0x20, /* parameter CRC on bank1 */

	/* CRC_CTRL status bit */
	PXI_TP_CRC_CTRL_BUSY = 0x01,
};

static gboolean
fu_pxi_tp_device_reset(FuPxiTpDevice *self, FuPxiTpResetMode mode, GError **error)
{
	guint8 key1 = PXI_TP_RESET_KEY1_MAGIC;
	guint8 key2 = 0;
	guint delay_ms = 0;

	switch (mode) {
	case PXI_TP_RESET_MODE_APPLICATION:
		key2 = PXI_TP_RESET_KEY2_REGULAR;
		delay_ms = 500;
		break;
	case PXI_TP_RESET_MODE_BOOTLOADER:
		key2 = PXI_TP_RESET_KEY2_BOOTLOADER;
		delay_ms = 10;
		break;
	default:
		g_return_val_if_reached(FALSE);
	}

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK1,
				      PXI_TP_R_SYS1_RESET_KEY1,
				      key1,
				      error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 30);

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK1,
				      PXI_TP_R_SYS1_RESET_KEY2,
				      key2,
				      error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), delay_ms);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_execute(FuPxiTpDevice *self,
			       guint8 inst_cmd,
			       guint32 ccr_cmd,
			       guint16 data_cnt,
			       GError **error)
{
	enum {
		PXI_TP_FLASH_EXECUTE_RETRY_MAX = 10,
		PXI_TP_FLASH_EXECUTE_RETRY_DELAY_MS = 1,
		PXI_TP_FLASH_EXECUTE_START = 0x01,
	};

	guint8 out_val = 0;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_INST_CMD,
				      inst_cmd,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_CCR0,
				      (guint8)((ccr_cmd >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_CCR1,
				      (guint8)((ccr_cmd >> 8) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_CCR2,
				      (guint8)((ccr_cmd >> 16) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_CCR3,
				      (guint8)((ccr_cmd >> 24) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_DATACNT0,
				      (guint8)((data_cnt >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_DATACNT1,
				      (guint8)((data_cnt >> 8) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_EXECUTE,
				      PXI_TP_FLASH_EXECUTE_START,
				      error))
		return FALSE;

	for (guint i = 0; i < PXI_TP_FLASH_EXECUTE_RETRY_MAX; i++) {
		fu_device_sleep(FU_DEVICE(self), PXI_TP_FLASH_EXECUTE_RETRY_DELAY_MS);

		if (!fu_pxi_tp_register_read(self,
					     PXI_TP_SYSTEM_BANK4,
					     PXI_TP_R_SYS4_FLASH_EXECUTE,
					     &out_val,
					     error))
			return FALSE;

		if (out_val == PXI_TP_FLASH_EXEC_STATE_SUCCESS)
			break;
	}

	if (out_val != PXI_TP_FLASH_EXEC_STATE_SUCCESS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "flash executes failure");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_write_enable(FuPxiTpDevice *self, GError **error)
{
	enum {
		PXI_TP_FLASH_WRITE_ENABLE_RETRY_MAX = 10,
		PXI_TP_FLASH_WRITE_ENABLE_RETRY_DELAY_MS = 1,
	};

	guint8 out_val = 0;

	if (!fu_pxi_tp_device_flash_execute(self,
					    PXI_TP_FLASH_INST_CMD0,
					    PXI_TP_FLASH_CCR_WRITE_ENABLE,
					    0,
					    error))
		return FALSE;

	for (guint i = 0; i < PXI_TP_FLASH_WRITE_ENABLE_RETRY_MAX; i++) {
		if (!fu_pxi_tp_device_flash_execute(self,
						    PXI_TP_FLASH_INST_CMD1,
						    PXI_TP_FLASH_CCR_READ_STATUS,
						    1,
						    error))
			return FALSE;

		fu_device_sleep(FU_DEVICE(self), PXI_TP_FLASH_WRITE_ENABLE_RETRY_DELAY_MS);

		if (!fu_pxi_tp_register_read(self,
					     PXI_TP_SYSTEM_BANK4,
					     PXI_TP_R_SYS4_FLASH_STATUS,
					     &out_val,
					     error))
			return FALSE;

		if ((out_val & PXI_TP_FLASH_WRITE_ENABLE_SUCCESS) != 0)
			break;
	}

	if ((out_val & PXI_TP_FLASH_WRITE_ENABLE_SUCCESS) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "flash write enable failure");
		g_debug("flash write enable failure");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_wait_busy(FuPxiTpDevice *self, GError **error)
{
	enum {
		PXI_TP_FLASH_BUSY_RETRY_MAX = 1000,
		PXI_TP_FLASH_BUSY_RETRY_DELAY_MS = 1,
	};

	guint8 out_val = 0;

	for (guint i = 0; i < PXI_TP_FLASH_BUSY_RETRY_MAX; i++) {
		if (!fu_pxi_tp_device_flash_execute(self,
						    PXI_TP_FLASH_INST_CMD1,
						    PXI_TP_FLASH_CCR_READ_STATUS,
						    1,
						    error))
			return FALSE;

		fu_device_sleep(FU_DEVICE(self), PXI_TP_FLASH_BUSY_RETRY_DELAY_MS);

		if (!fu_pxi_tp_register_read(self,
					     PXI_TP_SYSTEM_BANK4,
					     PXI_TP_R_SYS4_FLASH_STATUS,
					     &out_val,
					     error))
			return FALSE;

		/* busy bit cleared? */
		if ((out_val & PXI_TP_FLASH_STATUS_BUSY) == 0)
			break;
	}

	if ((out_val & PXI_TP_FLASH_STATUS_BUSY) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "flash wait busy failure");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_erase_sector(FuPxiTpDevice *self, guint8 sector, GError **error)
{
	guint32 flash_address = (guint32)(sector)*PXI_TP_SECTOR_SIZE;

	if (!fu_pxi_tp_device_flash_wait_busy(self, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_write_enable(self, error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR0,
				      (guint8)((flash_address >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR1,
				      (guint8)((flash_address >> 8) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR2,
				      (guint8)((flash_address >> 16) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR3,
				      (guint8)((flash_address >> 24) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_execute(self,
					    PXI_TP_FLASH_INST_CMD0,
					    PXI_TP_FLASH_CCR_ERASE_SECTOR,
					    0,
					    error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_program_256b_to_flash(FuPxiTpDevice *self,
					     guint8 sector,
					     guint8 page,
					     GError **error)
{
	guint32 flash_address =
	    (guint32)(sector)*PXI_TP_SECTOR_SIZE + (guint32)(page)*PXI_TP_PAGE_SIZE;

	if (!fu_pxi_tp_device_flash_wait_busy(self, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_write_enable(self, error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_BUF_ADDR0,
				      0x00,
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_BUF_ADDR1,
				      0x00,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR0,
				      (guint8)((flash_address >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR1,
				      (guint8)((flash_address >> 8) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR2,
				      (guint8)((flash_address >> 16) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK4,
				      PXI_TP_R_SYS4_FLASH_ADDR3,
				      (guint8)((flash_address >> 24) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_execute(self,
					    0x84,
					    PXI_TP_FLASH_CCR_PROGRAM_PAGE,
					    PXI_TP_PAGE_SIZE,
					    error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_write_sram_256b(FuPxiTpDevice *self, const guint8 *data, GError **error)
{
	enum {
		/*
		 * SRAM_TRIGGER (bank6)
		 * 0x00: enable NCS move, start transferring data to target SRAM address
		 * 0x01: disable NCS move
		 */
		PXI_TP_SRAM_TRIGGER_NCS_ENABLE = 0x00,
		PXI_TP_SRAM_TRIGGER_NCS_DISABLE = 0x01,
	};

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK6,
				      PXI_TP_R_SYS6_SRAM_ADDR0,
				      0x00,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK6,
				      PXI_TP_R_SYS6_SRAM_ADDR1,
				      0x00,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK6,
				      PXI_TP_R_SYS6_SRAM_SELECT,
				      self->sram_select,
				      error))
		return FALSE;

	/* enable NCS so that the following burst goes to SRAM buffer */
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK6,
				      PXI_TP_R_SYS6_SRAM_TRIGGER,
				      PXI_TP_SRAM_TRIGGER_NCS_ENABLE,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_burst_write(self, data, PXI_TP_PAGE_SIZE, error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "burst write buffer failure: ");
		g_debug("burst write buffer failure");
		return FALSE;
	}

	/* disable NCS and commit SRAM buffer to target address */
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK6,
				      PXI_TP_R_SYS6_SRAM_TRIGGER,
				      PXI_TP_SRAM_TRIGGER_NCS_DISABLE,
				      error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_firmware_clear(FuPxiTpDevice *self, FuPxiTpFirmware *ctn, GError **error)
{
	if (ctn == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware container is NULL");
		return FALSE;
	}

	guint32 start_address = fu_pxi_tp_firmware_get_firmware_address(ctn);

	if (!fu_pxi_tp_device_flash_erase_sector(self,
						 (guint8)(start_address / PXI_TP_SECTOR_SIZE),
						 error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "clear firmware failure: ");
		return FALSE;
	}

	return TRUE;
}

static guint32
fu_pxi_tp_device_crc_firmware(FuPxiTpDevice *self, GError **error)
{
	enum {
		/* polling parameters for firmware CRC */
		PXI_TP_CRC_FW_RETRY_MAX = 1000,
		PXI_TP_CRC_FW_RETRY_DELAY_MS = 10,
	};

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	/* read swap_flag from system bank4 */
	if (!fu_pxi_tp_register_read(self,
				     PXI_TP_SYSTEM_BANK4,
				     PXI_TP_R_SYS4_SWAP_FLAG,
				     &out_val,
				     error))
		return 0;
	swap_flag = out_val;

	/* read part_id from user bank0 (little-endian) */
	if (!fu_pxi_tp_register_read(self,
				     PXI_TP_USER_BANK0,
				     PXI_TP_R_USER0_PART_ID0,
				     &out_val,
				     error))
		return 0;
	part_id = out_val;

	if (!fu_pxi_tp_register_read(self,
				     PXI_TP_USER_BANK0,
				     PXI_TP_R_USER0_PART_ID1,
				     &out_val,
				     error))
		return 0;
	part_id |= (guint16)out_val << 8;

	switch (part_id) {
	case PXI_TP_PART_ID_PJP274:
		if (swap_flag != 0) {
			/* PJP274 + swap enabled → firmware on bank1 */
			if (!fu_pxi_tp_register_user_write(self,
							   PXI_TP_USER_BANK0,
							   PXI_TP_R_USER0_CRC_CTRL,
							   PXI_TP_CRC_CTRL_FW_BANK1,
							   error))
				return 0;
		} else {
			/* PJP274 normal boot → firmware on bank0 */
			if (!fu_pxi_tp_register_user_write(self,
							   PXI_TP_USER_BANK0,
							   PXI_TP_R_USER0_CRC_CTRL,
							   PXI_TP_CRC_CTRL_FW_BANK0,
							   error))
				return 0;
		}
		break;

	default:
		/* other part_id: always use bank0 firmware CRC */
		if (!fu_pxi_tp_register_user_write(self,
						   PXI_TP_USER_BANK0,
						   PXI_TP_R_USER0_CRC_CTRL,
						   PXI_TP_CRC_CTRL_FW_BANK0,
						   error))
			return 0;
		break;
	}

	/* wait CRC calculation completed */
	for (guint i = 0; i < PXI_TP_CRC_FW_RETRY_MAX; i++) {
		fu_device_sleep(FU_DEVICE(self), PXI_TP_CRC_FW_RETRY_DELAY_MS);

		if (!fu_pxi_tp_register_user_read(self,
						  PXI_TP_USER_BANK0,
						  PXI_TP_R_USER0_CRC_CTRL,
						  &out_val,
						  error))
			return 0;

		/* busy bit cleared? */
		if ((out_val & PXI_TP_CRC_CTRL_BUSY) == 0)
			break;
	}

	if ((out_val & PXI_TP_CRC_CTRL_BUSY) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware CRC wait busy failure");
		return 0;
	}

	/* read CRC result (32-bit, little-endian) */
	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT0,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val;

	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT1,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val << 8;

	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT2,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val << 16;

	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT3,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val << 24;

	g_debug("firmware CRC: 0x%08x", (guint)return_value);
	return return_value;
}
static guint32
fu_pxi_tp_device_crc_parameter(FuPxiTpDevice *self, GError **error)
{
	enum {
		/* polling parameters for parameter CRC */
		PXI_TP_CRC_PARAM_RETRY_MAX = 1000,
		PXI_TP_CRC_PARAM_RETRY_DELAY_MS = 10,
	};

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	/* read swap_flag from system bank4 */
	if (!fu_pxi_tp_register_read(self,
				     PXI_TP_SYSTEM_BANK4,
				     PXI_TP_R_SYS4_SWAP_FLAG,
				     &out_val,
				     error))
		return 0;
	swap_flag = out_val;

	/* read part_id from user bank0 (little-endian) */
	if (!fu_pxi_tp_register_read(self,
				     PXI_TP_USER_BANK0,
				     PXI_TP_R_USER0_PART_ID0,
				     &out_val,
				     error))
		return 0;
	part_id = out_val;

	if (!fu_pxi_tp_register_read(self,
				     PXI_TP_USER_BANK0,
				     PXI_TP_R_USER0_PART_ID1,
				     &out_val,
				     error))
		return 0;
	part_id |= (guint16)out_val << 8;

	switch (part_id) {
	case PXI_TP_PART_ID_PJP274:
		if (swap_flag != 0) {
			/* PJP274 + swap enabled → parameter on bank1 */
			if (!fu_pxi_tp_register_user_write(self,
							   PXI_TP_USER_BANK0,
							   PXI_TP_R_USER0_CRC_CTRL,
							   PXI_TP_CRC_CTRL_PARAM_BANK1,
							   error))
				return 0;
		} else {
			/* PJP274 normal boot → parameter on bank0 */
			if (!fu_pxi_tp_register_user_write(self,
							   PXI_TP_USER_BANK0,
							   PXI_TP_R_USER0_CRC_CTRL,
							   PXI_TP_CRC_CTRL_PARAM_BANK0,
							   error))
				return 0;
		}
		break;

	default:
		/* other part_id: always use bank0 parameter CRC */
		if (!fu_pxi_tp_register_user_write(self,
						   PXI_TP_USER_BANK0,
						   PXI_TP_R_USER0_CRC_CTRL,
						   PXI_TP_CRC_CTRL_PARAM_BANK0,
						   error))
			return 0;
		break;
	}

	/* wait CRC calculation completed */
	for (guint i = 0; i < PXI_TP_CRC_PARAM_RETRY_MAX; i++) {
		fu_device_sleep(FU_DEVICE(self), PXI_TP_CRC_PARAM_RETRY_DELAY_MS);

		if (!fu_pxi_tp_register_user_read(self,
						  PXI_TP_USER_BANK0,
						  PXI_TP_R_USER0_CRC_CTRL,
						  &out_val,
						  error))
			return 0;

		/* busy bit cleared? */
		if ((out_val & PXI_TP_CRC_CTRL_BUSY) == 0)
			break;
	}

	if ((out_val & PXI_TP_CRC_CTRL_BUSY) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "parameter CRC wait busy failure");
		return 0;
	}

	/* read CRC result (32-bit, little-endian) */
	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT0,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val;

	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT1,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val << 8;

	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT2,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val << 16;

	if (!fu_pxi_tp_register_user_read(self,
					  PXI_TP_USER_BANK0,
					  PXI_TP_R_USER0_CRC_RESULT3,
					  &out_val,
					  error))
		return 0;
	return_value |= (guint32)out_val << 24;

	g_debug("parameter CRC: 0x%08x", (guint)return_value);
	return return_value;
}

static gboolean
fu_pxi_tp_device_write_page(FuPxiTpDevice *self,
			    guint8 sector,
			    guint8 page,
			    const guint8 *src,
			    gsize total_sz,
			    gsize off,
			    GError **error)
{
	guint8 page_buf[PXI_TP_PAGE_SIZE] = {0};
	const gsize remain = total_sz - off;
	const gsize copy_len = remain < sizeof(page_buf) ? remain : sizeof(page_buf);

	if (copy_len == sizeof(page_buf)) {
		if (!fu_memcpy_safe(page_buf,
				    sizeof(page_buf),
				    0,
				    src,
				    total_sz,
				    off,
				    sizeof(page_buf),
				    error))
			return FALSE;
	} else {
		memset(page_buf, 0xFF, sizeof(page_buf));
		if (!fu_memcpy_safe(page_buf,
				    sizeof(page_buf),
				    0,
				    src,
				    total_sz,
				    off,
				    copy_len,
				    error))
			return FALSE;
	}

	if (!fu_pxi_tp_device_write_sram_256b(self, page_buf, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_program_256b_to_flash(self, sector, page, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_update_flash_process(FuPxiTpDevice *self,
				      FuProgress *progress,
				      guint32 data_size,
				      guint8 start_sector,
				      GByteArray *data,
				      GError **error)
{
	/* ---- basic validation / normalization ---- */
	if (self == NULL || progress == NULL || data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "update_flash: invalid args (self/progress/data is NULL)");
		return FALSE;
	}

	/* never read past provided payload */
	if ((gsize)data_size > data->len)
		data_size = (guint32)data->len;

	const guint8 *src = data->data;		 /* source blob */
	const gsize total_sz = (gsize)data_size; /* clamp as gsize for math */

	/* ceil-divide to sectors */
	const guint8 max_sector_cnt =
	    (total_sz == 0) ? 0
			    : (guint8)((total_sz + (PXI_TP_SECTOR_SIZE - 1)) / PXI_TP_SECTOR_SIZE);

	/* nothing to do */
	if (max_sector_cnt == 0)
		return TRUE;

	/* device-specific pre-write toggle (original behavior) */
	if (!fu_pxi_tp_register_write(self,
				      PXI_TP_SYSTEM_BANK2,
				      PXI_TP_R_SYS2_UPDATE_MODE,
				      0x02,
				      error))
		return FALSE;

	/* progress: 2 steps per sector (erase + program) */
	FuProgress *update_progress = fu_progress_get_child(progress);
	fu_progress_set_id(update_progress, G_STRLOC);
	fu_progress_add_flag(update_progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_set_steps(update_progress, max_sector_cnt * 2);

	/* ---------- ERASE PHASE ---------- */
	for (guint8 sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		if (!fu_pxi_tp_device_flash_erase_sector(self,
							 (guint8)(start_sector + sector_cnt),
							 error))
			return FALSE;
		fu_progress_step_done(update_progress);
	}

	/* ---------- PROGRAM PHASE ----------
	 * Keep the original order: write pages 1..15 first, then page 0.
	 * Each write is 256 bytes; last chunk in blob is padded with 0xFF.
	 */
	for (guint8 sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		const gsize sector_base = (gsize)sector_cnt * PXI_TP_SECTOR_SIZE;

		/* pages 1..15 */
		for (guint8 page_cnt = 1; page_cnt < PXI_TP_PAGES_COUNT_PER_SECTOR; page_cnt++) {
			const gsize off = sector_base + (gsize)page_cnt * PXI_TP_PAGE_SIZE;
			if (off >= total_sz)
				break;

			if (!fu_pxi_tp_device_write_page(self,
							 (guint8)(start_sector + sector_cnt),
							 page_cnt,
							 src,
							 total_sz,
							 off,
							 error))
				return FALSE;
		}

		/* page 0 last */
		{
			const guint8 page_cnt = 0;
			const gsize off = sector_base;

			if (off < total_sz) {
				if (!fu_pxi_tp_device_write_page(
					self,
					(guint8)(start_sector + sector_cnt),
					page_cnt,
					src,
					total_sz,
					off,
					error))
					return FALSE;
			}
		}

		fu_progress_step_done(update_progress);
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_setup(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 lo = 0, hi = 0;
	guint16 ver_u16 = 0;

	if (!fu_pxi_tp_register_user_read(self,
					  self->ver_bank,
					  (guint8)(self->ver_addr + 0),
					  &lo,
					  error))
		return FALSE;
	if (!fu_pxi_tp_register_user_read(self,
					  self->ver_bank,
					  (guint8)(self->ver_addr + 1),
					  &hi,
					  error))
		return FALSE;

	ver_u16 = (guint16)lo | ((guint16)hi << 8); /* low byte first */

	g_debug("pxi-tp setup: read version bytes: lo=0x%02x hi=0x%02x (LE) -> ver=0x%04x",
		(guint)lo,
		(guint)hi,
		(guint)ver_u16);

	g_autofree gchar *ver_str = g_strdup_printf("0x%04x", ver_u16);
	fu_device_set_version(device, ver_str);
	return TRUE;
}

static FuPxiTpFirmware *
fu_pxi_tp_device_wrap_or_parse_ctn(FuFirmware *maybe_generic, GError **error)
{
	if (FU_IS_PXI_TP_FIRMWARE(maybe_generic))
		return FU_PXI_TP_FIRMWARE(maybe_generic);

	g_autoptr(GBytes) bytes = fu_firmware_get_bytes_with_patches(maybe_generic, error);
	if (bytes == NULL)
		return NULL;

	g_autoptr(GInputStream) istream = g_memory_input_stream_new_from_bytes(bytes);
	FuFirmware *ctn = FU_FIRMWARE(g_object_new(FU_TYPE_PXI_TP_FIRMWARE, NULL));
	if (!fu_firmware_parse_stream(ctn, istream, 0, FU_FIRMWARE_PARSE_FLAG_NONE, error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "pxi-tp parse failed: ");
		g_object_unref(ctn);
		return NULL;
	}
	return FU_PXI_TP_FIRMWARE(ctn);
}

static gboolean
fu_pxi_tp_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	FuPxiTpFirmware *ctn = NULL;
	const GPtrArray *secs = NULL;
	FuProgress *prog_write = NULL;
	FuProgress *prog_verify = NULL;
	guint64 total_bytes = 0;
	guint64 written = 0;
	guint32 crc_value = 0;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);

	ctn = fu_pxi_tp_device_wrap_or_parse_ctn(firmware, error);
	if (ctn == NULL)
		return FALSE;

	secs = fu_pxi_tp_firmware_get_sections(ctn);
	if (secs == NULL || secs->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no sections to write");
		return FALSE;
	}

	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		if (s->is_valid_update && !s->is_external && s->section_length > 0)
			total_bytes += (guint64)s->section_length;
	}

	if (total_bytes == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no internal/valid sections to write");
		return FALSE;
	}

	prog_write = fu_progress_get_child(progress);
	fu_progress_set_id(prog_write, G_STRLOC);
	fu_progress_set_steps(prog_write, secs->len);

	/* erase old firmware */
	if (!fu_pxi_tp_device_firmware_clear(self, ctn, error))
		return FALSE;

	for (guint i = 0; i < secs->len; i++) {
		guint8 start_sector;
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);

		if (!s->is_valid_update || s->is_external || s->section_length == 0) {
			fu_progress_step_done(prog_write);
			continue;
		}

		g_autoptr(GByteArray) data =
		    fu_pxi_tp_firmware_get_slice_by_file(ctn,
							 (gsize)s->internal_file_start,
							 (gsize)s->section_length,
							 error);
		if (data == NULL)
			return FALSE;

		start_sector = (guint8)(s->target_flash_start / PXI_TP_SECTOR_SIZE);

		g_debug("pxi-tp write section %u: flash=0x%08x, file_off=0x%08" G_GINT64_MODIFIER
			"x, len=%u, sector=%u, data_len=%u",
			i,
			s->target_flash_start,
			(gint64)s->internal_file_start,
			(guint)s->section_length,
			start_sector,
			(guint)data->len);

		if (data->len == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "empty payload for section %u",
				    i);
			return FALSE;
		}

		switch (s->update_type) {
		case PXI_TP_UPDATE_TYPE_GENERAL:
		case PXI_TP_UPDATE_TYPE_FW_SECTION:
		case PXI_TP_UPDATE_TYPE_PARAM:
			if (!fu_pxi_tp_device_update_flash_process(self,
								   prog_write,
								   (guint)s->section_length,
								   start_sector,
								   data,
								   error)) {
				return FALSE;
			}
			written += (guint64)s->section_length;
			break;

		case PXI_TP_UPDATE_TYPE_TF_FORCE: {
			/* target TF version is stored in s->reserved[0..2] */
			guint8 target_ver[3] = {s->reserved[0], s->reserved[1], s->reserved[2]};

			if (!fu_pxi_tp_device_reset(self, PXI_TP_RESET_MODE_APPLICATION, error))
				return FALSE;

			guint32 send_interval = (guint32)s->reserved[3]; /* ms */
			g_debug("send interval (ms): %u", send_interval);
			g_debug("update TF firmware, section %u, len=%u", i, (guint)data->len);

			if (!fu_pxi_tp_tf_communication_write_firmware_process(self,
									       prog_write,
									       send_interval,
									       (guint32)data->len,
									       data,
									       target_ver,
									       error)) {
				return FALSE;
			}

			if (!fu_pxi_tp_device_reset(self, PXI_TP_RESET_MODE_BOOTLOADER, error))
				return FALSE;

			written += (guint64)data->len;
			break;
		}

		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "not support update type for section %u",
				    i);
			return FALSE;
		}

		fu_progress_step_done(prog_write);
	}

	fu_progress_step_done(progress);

	prog_verify = fu_progress_get_child(progress);
	fu_progress_set_id(prog_verify, G_STRLOC);
	fu_progress_set_steps(prog_verify, 2);

	/* crc check */
	if (!fu_pxi_tp_device_reset(self, PXI_TP_RESET_MODE_BOOTLOADER, error))
		return FALSE;

	crc_value = fu_pxi_tp_device_crc_firmware(self, error);
	if (error != NULL && *error != NULL)
		return FALSE;

	if (crc_value != fu_pxi_tp_firmware_get_file_firmware_crc(ctn)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Firmware CRC compare failed");
		(void)fu_pxi_tp_device_firmware_clear(self, ctn, NULL);
		return FALSE;
	}

	fu_progress_step_done(prog_verify);

	crc_value = fu_pxi_tp_device_crc_parameter(self, error);
	if (error != NULL && *error != NULL)
		return FALSE;

	if (crc_value != fu_pxi_tp_firmware_get_file_parameter_crc(ctn)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Parameter CRC compare failed");
		(void)fu_pxi_tp_device_firmware_clear(self, ctn, error);
		return FALSE;
	}

	fu_progress_step_done(prog_verify);

	fu_progress_step_done(progress);
	g_debug("update success (written=%" G_GUINT64_FORMAT " / total=%" G_GUINT64_FORMAT ")",
		written,
		total_bytes);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (g_strcmp0(key, "HidVersionReg") == 0) {
		GStrv parts;
		guint i;

		/* allow: "bank=0x00; addr=0x0b" (fixed 2 bytes, LE) */
		parts = g_strsplit(value, ";", -1);

		for (i = 0; parts && parts[i]; i++) {
			gchar *kv;
			GStrv kvp = NULL;
			const gchar *k = NULL;
			const gchar *v = NULL;
			guint64 tmp = 0;

			kv = g_strstrip(parts[i]);
			if (*kv == '\0')
				continue;

			kvp = g_strsplit(kv, "=", 2);
			if (!kvp[0] || !kvp[1]) {
				g_strfreev(kvp);
				continue;
			}

			k = g_strstrip(kvp[0]);
			v = g_strstrip(kvp[1]);

			if (g_ascii_strcasecmp(k, "bank") == 0) {
				if (!fu_strtoull(v, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error)) {
					g_strfreev(kvp);
					g_strfreev(parts);
					return FALSE;
				}
				self->ver_bank = (guint8)tmp;

			} else if (g_ascii_strcasecmp(k, "addr") == 0) {
				if (!fu_strtoull(v, &tmp, 0, 0xffff, FU_INTEGER_BASE_AUTO, error)) {
					g_strfreev(kvp);
					g_strfreev(parts);
					return FALSE;
				}
				self->ver_addr = (guint16)tmp;
			}

			g_strfreev(kvp);
		}

		g_strfreev(parts);

		g_debug("quirk: HidVersionReg parsed => bank=0x%02x addr=0x%04x",
			(guint)self->ver_bank,
			(guint)self->ver_addr);
		return TRUE;
	}

	if (g_strcmp0(key, "SramSelect") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_select = (guint8)tmp;
		g_debug("quirk: SramSelect parsed => 0x%02x", (guint)self->sram_select);
		return TRUE;
	}

	/* unknown quirk */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "quirk key not supported: %s",
		    key);
	return FALSE;
}

static gboolean
fu_pxi_tp_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pxi_tp_device_setup(device, error))
			g_warning("failed to refresh version (already attached)");
		return TRUE;
	}

	if (!fu_pxi_tp_device_reset(FU_PXI_TP_DEVICE(device), PXI_TP_RESET_MODE_APPLICATION, error))
		return FALSE;

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_debug("exit bootloader");

	if (!fu_pxi_tp_device_setup(device, error))
		g_warning("failed to refresh version after attached");

	return TRUE;
}

static gboolean
fu_pxi_tp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pxi_tp_device_reset(FU_PXI_TP_DEVICE(device), PXI_TP_RESET_MODE_BOOTLOADER, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_debug("enter bootloader");
	return TRUE;
}

static gboolean
fu_pxi_tp_device_cleanup(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	g_debug("fu_pxi_tp_device_cleanup");
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pxi_tp_device_reset(FU_PXI_TP_DEVICE(device),
					    PXI_TP_RESET_MODE_APPLICATION,
					    error))
			return FALSE;
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		g_debug("exit bootloader");
	}

	return TRUE; /* cleanup should avoid reporting errors if possible */
}

static void
fu_pxi_tp_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_pxi_tp_device_init(FuPxiTpDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.tp");
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	/* quirk default value */
	self->sram_select = 0x0f;
	self->ver_bank = 0x00;
	self->ver_addr = 0x0b;
}

static void
fu_pxi_tp_device_class_init(FuPxiTpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_pxi_tp_device_setup;
	klass_device->write_firmware = fu_pxi_tp_device_write_firmware;
	klass_device->attach = fu_pxi_tp_device_attach;
	klass_device->detach = fu_pxi_tp_device_detach;
	klass_device->cleanup = fu_pxi_tp_device_cleanup;
	klass_device->set_progress = fu_pxi_tp_device_set_progress;
	klass_device->set_quirk_kv = fu_pxi_tp_device_set_quirk_kv;
}
