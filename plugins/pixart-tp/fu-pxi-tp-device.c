/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-fw-struct.h"
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

/* ---- flash properties ---- */

#define PXI_TP_SECTOR_SIZE	      4096
#define PXI_TP_PAGE_SIZE	      256
#define PXI_TP_PAGES_COUNT_PER_SECTOR 16

static gboolean
fu_pxi_tp_device_reset(FuPxiTpDevice *self, guint8 mode, GError **error)
{
	guint8 key1 = FU_PXI_TP_RESET_KEY1_SUSPEND;
	guint8 key2 = 0;
	guint delay_ms = 0;

	switch (mode) {
	case FU_PXI_TP_RESET_MODE_APPLICATION:
		key2 = FU_PXI_TP_RESET_KEY2_REGULAR;
		delay_ms = 500;
		break;
	case FU_PXI_TP_RESET_MODE_BOOTLOADER:
		key2 = FU_PXI_TP_RESET_KEY2_BOOTLOADER;
		delay_ms = 10;
		break;
	default:
		g_return_val_if_reached(FALSE);
	}

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK1,
				      FU_PXI_TP_REG_SYS1_RESET_KEY1,
				      key1,
				      error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 30);

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK1,
				      FU_PXI_TP_REG_SYS1_RESET_KEY2,
				      key2,
				      error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), delay_ms);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_execute_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 *out_val = user_data;

	if (!fu_pxi_tp_register_read(self,
				     FU_PXI_TP_SYSTEM_BANK_BANK4,
				     FU_PXI_TP_REG_SYS4_FLASH_EXECUTE,
				     out_val,
				     error))
		return FALSE;

	if (*out_val != FU_PXI_TP_FLASH_EXEC_STATE_SUCCESS) {
		/* not ready yet, ask fu_device_retry_full() to try again */
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "flash execute still in progress");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_execute(FuPxiTpDevice *self,
			       guint8 inst_cmd,
			       guint32 ccr_cmd,
			       guint16 data_cnt,
			       GError **error)
{
	const guint flash_execute_retry_max = 10;
	const guint flash_execute_retry_delay_ms = 1;
	const guint8 flash_execute_start = 0x01;

	guint8 out_val = 0;

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_INST_CMD,
				      inst_cmd,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_CCR0,
				      (guint8)((ccr_cmd >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_CCR1,
				      (guint8)((ccr_cmd >> 8) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_CCR2,
				      (guint8)((ccr_cmd >> 16) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_CCR3,
				      (guint8)((ccr_cmd >> 24) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_DATA_CNT0,
				      (guint8)((data_cnt >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_DATA_CNT1,
				      (guint8)((data_cnt >> 8) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_EXECUTE,
				      flash_execute_start,
				      error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_device_flash_execute_wait_cb,
				  flash_execute_retry_max,
				  flash_execute_retry_delay_ms,
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "flash executes failure: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_write_enable_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 *out_val = user_data;
	const guint flash_write_enable_status_delay_ms = 1;

	/* send READ_STATUS command */
	if (!fu_pxi_tp_device_flash_execute(self,
					    FU_PXI_TP_FLASH_INST_CMD1,
					    FU_PXI_TP_FLASH_CCR_READ_STATUS,
					    1,
					    error))
		return FALSE;

	/* small delay between command and status read */
	fu_device_sleep(device, flash_write_enable_status_delay_ms);

	/* read FLASH_STATUS register */
	if (!fu_pxi_tp_register_read(self,
				     FU_PXI_TP_SYSTEM_BANK_BANK4,
				     FU_PXI_TP_REG_SYS4_FLASH_STATUS,
				     out_val,
				     error))
		return FALSE;

	/* check WEL bit */
	if ((*out_val & FU_PXI_TP_FLASH_WRITE_ENABLE_SUCCESS) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "flash write enable still not set");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_write_enable(FuPxiTpDevice *self, GError **error)
{
	const guint flash_write_enable_retry_max = 10;
	const guint flash_write_enable_retry_delay_ms = 0;

	guint8 out_val = 0;

	/* send WRITE_ENABLE once */
	if (!fu_pxi_tp_device_flash_execute(self,
					    FU_PXI_TP_FLASH_INST_CMD0,
					    FU_PXI_TP_FLASH_CCR_WRITE_ENABLE,
					    0,
					    error))
		return FALSE;

	/* poll WEL bit using fu_device_retry_full() */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_device_flash_write_enable_wait_cb,
				  flash_write_enable_retry_max,
				  flash_write_enable_retry_delay_ms,
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "flash write enable failure: ");
		g_debug("flash write enable failure");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_wait_busy_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 *out_val = user_data;
	const guint flash_busy_status_delay_ms = 1;

	/* send READ_STATUS command */
	if (!fu_pxi_tp_device_flash_execute(self,
					    FU_PXI_TP_FLASH_INST_CMD1,
					    FU_PXI_TP_FLASH_CCR_READ_STATUS,
					    1,
					    error))
		return FALSE;

	/* small delay before reading status */
	fu_device_sleep(device, flash_busy_status_delay_ms);

	/* read FLASH_STATUS register */
	if (!fu_pxi_tp_register_read(self,
				     FU_PXI_TP_SYSTEM_BANK_BANK4,
				     FU_PXI_TP_REG_SYS4_FLASH_STATUS,
				     out_val,
				     error))
		return FALSE;

	/* busy bit cleared? */
	if ((*out_val & FU_PXI_TP_FLASH_STATUS_BUSY) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "flash still busy");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_wait_busy(FuPxiTpDevice *self, GError **error)
{
	const guint flash_busy_retry_max = 1000;
	const guint flash_busy_retry_delay_ms = 0;
	guint8 out_val = 0;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_device_flash_wait_busy_cb,
				  flash_busy_retry_max,
				  flash_busy_retry_delay_ms,
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "flash wait busy failure: ");
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
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR0,
				      (guint8)((flash_address >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR1,
				      (guint8)((flash_address >> 8) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR2,
				      (guint8)((flash_address >> 16) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR3,
				      (guint8)((flash_address >> 24) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_execute(self,
					    FU_PXI_TP_FLASH_INST_CMD0,
					    FU_PXI_TP_FLASH_CCR_ERASE_SECTOR,
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
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_BUF_ADDR0,
				      0x00,
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_BUF_ADDR1,
				      0x00,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR0,
				      (guint8)((flash_address >> 0) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR1,
				      (guint8)((flash_address >> 8) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR2,
				      (guint8)((flash_address >> 16) & 0xff),
				      error))
		return FALSE;
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK4,
				      FU_PXI_TP_REG_SYS4_FLASH_ADDR3,
				      (guint8)((flash_address >> 24) & 0xff),
				      error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_execute(self,
					    0x84,
					    FU_PXI_TP_FLASH_CCR_PROGRAM_PAGE,
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
				      FU_PXI_TP_SYSTEM_BANK_BANK6,
				      FU_PXI_TP_REG_SYS6_SRAM_ADDR0,
				      0x00,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK6,
				      FU_PXI_TP_REG_SYS6_SRAM_ADDR1,
				      0x00,
				      error))
		return FALSE;

	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK6,
				      FU_PXI_TP_REG_SYS6_SRAM_SELECT,
				      self->sram_select,
				      error))
		return FALSE;

	/* enable NCS so that the following burst goes to SRAM buffer */
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK6,
				      FU_PXI_TP_REG_SYS6_SRAM_TRIGGER,
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
				      FU_PXI_TP_SYSTEM_BANK_BANK6,
				      FU_PXI_TP_REG_SYS6_SRAM_TRIGGER,
				      PXI_TP_SRAM_TRIGGER_NCS_DISABLE,
				      error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_firmware_clear(FuPxiTpDevice *self, FuPxiTpFirmware *ctn, GError **error)
{
	guint32 start_address = 0;

	if (ctn == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware container is NULL");
		return FALSE;
	}

	start_address = fu_pxi_tp_firmware_get_firmware_address(ctn);

	if (!fu_pxi_tp_device_flash_erase_sector(self,
						 (guint8)(start_address / PXI_TP_SECTOR_SIZE),
						 error)) {
		if (error != NULL && *error != NULL)
			g_prefix_error_literal(error, "clear firmware failure: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_crc_firmware_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 *out_val = user_data;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_CTRL,
					  out_val,
					  error))
		return FALSE;

	/* busy bit cleared? */
	if ((*out_val & FU_PXI_TP_CRC_CTRL_BUSY) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware CRC still busy");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_pxi_tp_device_crc_firmware(FuPxiTpDevice *self, guint32 *crc, GError **error)
{
	const guint crc_fw_retry_max = 1000;
	const guint crc_fw_retry_delay_ms = 10;

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	g_return_val_if_fail(crc != NULL, FALSE);
	*crc = 0;

	/* read swap_flag from system bank4 */
	if (!fu_pxi_tp_register_read(self,
				     FU_PXI_TP_SYSTEM_BANK_BANK4,
				     FU_PXI_TP_REG_SYS4_SWAP_FLAG,
				     &out_val,
				     error))
		return FALSE;
	swap_flag = out_val;

	/* read part_id from user bank0 (little-endian) */
	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_PART_ID0,
					  &out_val,
					  error))
		return FALSE;
	part_id = out_val;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_PART_ID1,
					  &out_val,
					  error))
		return FALSE;
	part_id |= (guint16)out_val << 8;

	switch (part_id) {
	case FU_PXI_TP_PART_ID_PJP274:
		if (swap_flag != 0) {
			/* PJP274 + swap enabled → firmware on bank1 */
			if (!fu_pxi_tp_register_user_write(self,
							   FU_PXI_TP_USER_BANK_BANK0,
							   FU_PXI_TP_REG_USER0_CRC_CTRL,
							   FU_PXI_TP_CRC_CTRL_FW_BANK1,
							   error))
				return FALSE;
		} else {
			/* PJP274 normal boot → firmware on bank0 */
			if (!fu_pxi_tp_register_user_write(self,
							   FU_PXI_TP_USER_BANK_BANK0,
							   FU_PXI_TP_REG_USER0_CRC_CTRL,
							   FU_PXI_TP_CRC_CTRL_FW_BANK0,
							   error))
				return FALSE;
		}
		break;

	default:
		/* other part_id: always use bank0 firmware CRC */
		if (!fu_pxi_tp_register_user_write(self,
						   FU_PXI_TP_USER_BANK_BANK0,
						   FU_PXI_TP_REG_USER0_CRC_CTRL,
						   FU_PXI_TP_CRC_CTRL_FW_BANK0,
						   error))
			return FALSE;
		break;
	}

	/* wait CRC calculation completed */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_device_crc_firmware_wait_cb,
				  crc_fw_retry_max,
				  crc_fw_retry_delay_ms,
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "firmware CRC wait busy failure: ");
		return FALSE;
	}

	/* read CRC result (32-bit, little-endian) */
	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT0,
					  &out_val,
					  error))
		return FALSE;
	return_value |= (guint32)out_val;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT1,
					  &out_val,
					  error))
		return FALSE;
	return_value |= (guint32)out_val << 8;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT2,
					  &out_val,
					  error))
		return FALSE;
	return_value |= (guint32)out_val << 16;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT3,
					  &out_val,
					  error))
		return FALSE;
	return_value |= (guint32)out_val << 24;

	*crc = return_value;
	g_debug("firmware CRC: 0x%08x", (guint)*crc);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_crc_parameter_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 *out_val = user_data;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_CTRL,
					  out_val,
					  error))
		return FALSE;

	/* busy bit cleared? */
	if ((*out_val & FU_PXI_TP_CRC_CTRL_BUSY) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "parameter CRC still busy");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_pxi_tp_device_crc_parameter(FuPxiTpDevice *self, guint32 *crc, GError **error)
{
	const guint crc_param_retry_max = 1000;
	const guint crc_param_retry_delay_ms = 10;

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 result = 0;

	g_return_val_if_fail(crc != NULL, FALSE);
	*crc = 0;

	/* read swap_flag from system bank4 */
	if (!fu_pxi_tp_register_read(self,
				     FU_PXI_TP_SYSTEM_BANK_BANK4,
				     FU_PXI_TP_REG_SYS4_SWAP_FLAG,
				     &out_val,
				     error))
		return FALSE;
	swap_flag = out_val;

	/* read part_id from user bank0 (little-endian) */
	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_PART_ID0,
					  &out_val,
					  error))
		return FALSE;
	part_id = out_val;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_PART_ID1,
					  &out_val,
					  error))
		return FALSE;
	part_id |= (guint16)out_val << 8;

	/* select CRC source */
	switch (part_id) {
	case FU_PXI_TP_PART_ID_PJP274:
		if (swap_flag != 0) {
			if (!fu_pxi_tp_register_user_write(self,
							   FU_PXI_TP_USER_BANK_BANK0,
							   FU_PXI_TP_REG_USER0_CRC_CTRL,
							   FU_PXI_TP_CRC_CTRL_PARAM_BANK1,
							   error))
				return FALSE;
		} else {
			if (!fu_pxi_tp_register_user_write(self,
							   FU_PXI_TP_USER_BANK_BANK0,
							   FU_PXI_TP_REG_USER0_CRC_CTRL,
							   FU_PXI_TP_CRC_CTRL_PARAM_BANK0,
							   error))
				return FALSE;
		}
		break;

	default:
		if (!fu_pxi_tp_register_user_write(self,
						   FU_PXI_TP_USER_BANK_BANK0,
						   FU_PXI_TP_REG_USER0_CRC_CTRL,
						   FU_PXI_TP_CRC_CTRL_PARAM_BANK0,
						   error))
			return FALSE;
		break;
	}

	/* wait CRC calculation completed */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pxi_tp_device_crc_parameter_wait_cb,
				  crc_param_retry_max,
				  crc_param_retry_delay_ms,
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "parameter CRC wait busy failure: ");
		return FALSE;
	}

	/* read CRC result (32-bit LE) */
	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT0,
					  &out_val,
					  error))
		return FALSE;
	result |= (guint32)out_val;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT1,
					  &out_val,
					  error))
		return FALSE;
	result |= (guint32)out_val << 8;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT2,
					  &out_val,
					  error))
		return FALSE;
	result |= (guint32)out_val << 16;

	if (!fu_pxi_tp_register_user_read(self,
					  FU_PXI_TP_USER_BANK_BANK0,
					  FU_PXI_TP_REG_USER0_CRC_RESULT3,
					  &out_val,
					  error))
		return FALSE;
	result |= (guint32)out_val << 24;

	*crc = result;
	g_debug("parameter CRC: 0x%08x", result);

	return TRUE;
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
	gsize remain = total_sz - off;
	gsize copy_len = MIN(remain, sizeof(page_buf));

	/* initialize all bytes to 0xFF */
	memset(page_buf, 0xFF, sizeof(page_buf));

	/* copy actual payload */
	if (!fu_memcpy_safe(page_buf, sizeof(page_buf), 0, src, total_sz, off, copy_len, error))
		return FALSE;

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
	const guint8 *src = NULL;
	gsize total_sz = 0;
	guint8 max_sector_cnt = 0;
	FuProgress *update_progress = NULL;

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

	src = data->data;	     /* source blob */
	total_sz = (gsize)data_size; /* clamp as gsize for math */

	/* ceil-divide to sectors */
	if (total_sz == 0)
		max_sector_cnt = 0;
	else
		max_sector_cnt =
		    (guint8)((total_sz + (PXI_TP_SECTOR_SIZE - 1)) / PXI_TP_SECTOR_SIZE);

	/* nothing to do */
	if (max_sector_cnt == 0)
		return TRUE;

	/* device-specific pre-write toggle (original behavior) */
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK2,
				      FU_PXI_TP_REG_SYS2_UPDATE_MODE,
				      0x02,
				      error))
		return FALSE;

	/* progress: 2 steps per sector (erase + program) */
	update_progress = fu_progress_get_child(progress);
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
		gsize sector_base = (gsize)sector_cnt * PXI_TP_SECTOR_SIZE;

		/* pages 1..15 */
		for (guint8 page_cnt = 1; page_cnt < PXI_TP_PAGES_COUNT_PER_SECTOR; page_cnt++) {
			gsize off = sector_base + (gsize)page_cnt * PXI_TP_PAGE_SIZE;

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
			guint8 page_cnt0 = 0;
			gsize off0 = sector_base;

			if (off0 < total_sz) {
				if (!fu_pxi_tp_device_write_page(
					self,
					(guint8)(start_sector + sector_cnt),
					page_cnt0,
					src,
					total_sz,
					off0,
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
	guint8 lo = 0;
	guint8 hi = 0;
	guint16 ver_u16 = 0;
	g_autofree gchar *ver_str = NULL;

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

	ver_str = g_strdup_printf("0x%04x", ver_u16);
	fu_device_set_version(device, ver_str);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_process_section(FuPxiTpDevice *self,
				 FuPxiTpFirmware *ctn,
				 FuPxiTpSection *s,
				 guint section_index,
				 FuProgress *prog_write,
				 guint8 start_sector,
				 guint64 *written,
				 GError **error)
{
	g_autoptr(GByteArray) data = NULL;
	guint32 send_interval = 0;
	guint8 target_ver[3] = {0};

	data = fu_pxi_tp_firmware_get_slice_by_file(ctn,
						    (gsize)s->internal_file_start,
						    (gsize)s->section_length,
						    error);
	if (data == NULL)
		return FALSE;

	if (data->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "empty payload for section %u",
			    section_index);
		return FALSE;
	}

	g_debug("pxi-tp write section %u: flash=0x%08x, "
		"file_off=0x%08" G_GINT64_MODIFIER "x, len=%u, sector=%u, data_len=%u",
		section_index,
		s->target_flash_start,
		(guint64)s->internal_file_start,
		(guint)s->section_length,
		start_sector,
		(guint)data->len);

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
		*written += (guint64)s->section_length;
		break;

	case PXI_TP_UPDATE_TYPE_TF_FORCE:
		/* target TF version is stored in s->reserved[0..2] */
		target_ver[0] = s->reserved[0];
		target_ver[1] = s->reserved[1];
		target_ver[2] = s->reserved[2];
		send_interval = (guint32)s->reserved[3]; /* ms */

		if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_APPLICATION, error))
			return FALSE;

		g_debug("send interval (ms): %u", send_interval);
		g_debug("update TF firmware, section %u, len=%u", section_index, (guint)data->len);

		if (!fu_pxi_tp_tf_communication_write_firmware_process(self,
								       prog_write,
								       send_interval,
								       (guint32)data->len,
								       data,
								       target_ver,
								       error)) {
			return FALSE;
		}

		if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_BOOTLOADER, error))
			return FALSE;

		*written += (guint64)data->len;
		break;

	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "not support update type for section %u",
			    section_index);
		return FALSE;
	}

	fu_progress_step_done(prog_write);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_verify_crc(FuPxiTpDevice *self,
			    FuPxiTpFirmware *ctn,
			    FuProgress *progress,
			    GError **error)
{
	FuProgress *prog_verify = NULL;
	guint32 crc_value = 0;

	prog_verify = fu_progress_get_child(progress);
	fu_progress_set_id(prog_verify, G_STRLOC);
	fu_progress_set_steps(prog_verify, 2);

	/* reset to bootloader before CRC check */
	if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_BOOTLOADER, error))
		return FALSE;

	/* firmware CRC */
	if (!fu_pxi_tp_device_crc_firmware(self, &crc_value, error))
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

	/* parameter CRC */
	if (!fu_pxi_tp_device_crc_parameter(self, &crc_value, error))
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

	return TRUE;
}

static gboolean
fu_pxi_tp_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	FuPxiTpFirmware *fw_container = NULL;
	const GPtrArray *sections = NULL;
	FuProgress *progress_write = NULL;
	guint64 total_update_bytes = 0;
	guint64 total_written_bytes = 0;
	guint section_idx = 0;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);

	fw_container = FU_PXI_TP_FIRMWARE(firmware);
	sections = fu_pxi_tp_firmware_get_sections(fw_container);
	if (sections == NULL || sections->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no sections to write");
		return FALSE;
	}

	/* calculate total bytes for valid internal sections */
	for (section_idx = 0; section_idx < sections->len; section_idx++) {
		FuPxiTpSection *section = g_ptr_array_index((GPtrArray *)sections, section_idx);

		if (section->is_valid_update && !section->is_external &&
		    section->section_length > 0)
			total_update_bytes += (guint64)section->section_length;
	}

	if (total_update_bytes == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no internal/valid sections to write");
		return FALSE;
	}

	progress_write = fu_progress_get_child(progress);
	fu_progress_set_id(progress_write, G_STRLOC);
	fu_progress_set_steps(progress_write, sections->len);

	/* erase old firmware */
	if (!fu_pxi_tp_device_firmware_clear(self, fw_container, error))
		return FALSE;

	/* program all sections */
	for (section_idx = 0; section_idx < sections->len; section_idx++) {
		FuPxiTpSection *section = g_ptr_array_index((GPtrArray *)sections, section_idx);

		/* skip non-updatable sections */
		if (!section->is_valid_update || section->is_external ||
		    section->section_length == 0) {
			fu_progress_step_done(progress_write);
			continue;
		}

		guint8 flash_sector_start =
		    (guint8)(section->target_flash_start / PXI_TP_SECTOR_SIZE);

		if (!fu_pxi_tp_device_process_section(self,
						      fw_container,
						      section,
						      section_idx,
						      progress_write,
						      flash_sector_start,
						      &total_written_bytes,
						      error))
			return FALSE;
	}

	fu_progress_step_done(progress);

	/* verify CRC (firmware + parameter) */
	if (!fu_pxi_tp_device_verify_crc(self, fw_container, progress, error))
		return FALSE;

	fu_progress_step_done(progress);

	g_debug("update success (written=%" G_GUINT64_FORMAT " / total=%" G_GUINT64_FORMAT ")",
		total_written_bytes,
		total_update_bytes);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "PxiTpHidVersionBank") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ver_bank = (guint8)tmp;
		g_debug("quirk: PxiTpHidVersionBank => 0x%02x", self->ver_bank);
		return TRUE;
	}

	if (g_strcmp0(key, "PxiTpHidVersionAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xffff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ver_addr = (guint16)tmp;
		g_debug("quirk: PxiTpHidVersionAddr => 0x%04x", self->ver_addr);
		return TRUE;
	}

	if (g_strcmp0(key, "PxiTpSramSelect") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_select = (guint8)tmp;
		g_debug("quirk: PxiTpSramSelect => 0x%02x", self->sram_select);
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
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	/* Normal runtime attach:
	 * - Version should already be set in ->setup()
	 * - Do not talk to hardware again here, and certainly do not warn
	 *   if the device is already attached or emulated.
	 */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* coming back from bootloader into application mode */
	if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_APPLICATION, error))
		return FALSE;

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_debug("exit bootloader");

	/* Best-effort refresh of the version string.
	 * In self-tests or emulated environments this may fail, and that's fine:
	 * do NOT emit warnings here, as G_DEBUG=fatal-criticals would abort.
	 */
	{
		g_autoptr(GError) local_error = NULL;

		if (!fu_pxi_tp_device_setup(device, &local_error)) {
			g_debug("failed to refresh version after attach: %s",
				local_error != NULL ? local_error->message : "unknown");
		}
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pxi_tp_device_reset(FU_PXI_TP_DEVICE(device),
				    FU_PXI_TP_RESET_MODE_BOOTLOADER,
				    error))
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
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_debug("fu_pxi_tp_tf_device_cleanup");

	/* exit upgrade mode (best-effort) */
	if (!fu_pxi_tp_tf_communication_exit_upgrade_mode(self, NULL))
		g_debug("failed to exit upgrade mode (ignored)");

	g_debug("fu_pxi_tp_device_cleanup");
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_APPLICATION, error))
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

static FuFirmware *
fu_pxi_tp_device_prepare_firmware(FuDevice *device,
				  GInputStream *stream,
				  FuProgress *progress,
				  FuFirmwareParseFlags flags,
				  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_pxi_tp_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
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
	klass_device->prepare_firmware = fu_pxi_tp_device_prepare_firmware;
}
