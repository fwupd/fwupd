/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-haptic-device.h"
#include "fu-pxi-tp-register.h"
#include "fu-pxi-tp-section.h"
#include "fu-pxi-tp-struct.h"

struct _FuPxiTpDevice {
	FuHidrawDevice parent_instance;

	guint8 sram_select;
	guint8 ver_bank;
	guint16 ver_addr;
	gboolean has_tf_child;
};

G_DEFINE_TYPE(FuPxiTpDevice, fu_pxi_tp_device, FU_TYPE_HIDRAW_DEVICE)

/* ---- flash properties ---- */

#define PXI_TP_SECTOR_SIZE	      4096
#define PXI_TP_PAGE_SIZE	      256
#define PXI_TP_PAGES_COUNT_PER_SECTOR 16

static gboolean
fu_pxi_tp_device_reset(FuPxiTpDevice *self, FuPxiTpResetMode mode, GError **error)
{
	FuPxiTpResetKey1 key1 = FU_PXI_TP_RESET_KEY1_SUSPEND;
	FuPxiTpResetKey2 key2 = 0;
	guint delay_ms = 0;

	if (mode == FU_PXI_TP_RESET_MODE_APPLICATION) {
		key2 = FU_PXI_TP_RESET_KEY2_REGULAR;
		delay_ms = 500;
	} else {
		key2 = FU_PXI_TP_RESET_KEY2_BOOTLOADER;
		delay_ms = 10;
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

	/* success */
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

	/* success */
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

	/* success */
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

	/* success */
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

	/* success */
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

	/* success */
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

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_erase_sector(FuPxiTpDevice *self, guint8 sector, GError **error)
{
	guint32 flash_address = (guint32)sector * PXI_TP_SECTOR_SIZE;

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

	g_debug("pxi-tp: erase sector %u (addr=0x%08x)", (guint)sector, flash_address);

	if (!fu_pxi_tp_device_flash_execute(self,
					    FU_PXI_TP_FLASH_INST_CMD0,
					    FU_PXI_TP_FLASH_CCR_ERASE_SECTOR,
					    0,
					    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_program_256b_to_flash(FuPxiTpDevice *self,
					     guint8 sector,
					     guint8 page,
					     GError **error)
{
	guint32 flash_address =
	    (guint32)sector * PXI_TP_SECTOR_SIZE + (guint32)page * PXI_TP_PAGE_SIZE;

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

	/* success */
	return TRUE;
}

/* ========================================================================== */
/*                           SRAM write (256 bytes)                           */
/* ========================================================================== */

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
		g_prefix_error_literal(error, "burst write buffer failure: ");
		return FALSE;
	}

	/* disable NCS and commit SRAM buffer to target address */
	if (!fu_pxi_tp_register_write(self,
				      FU_PXI_TP_SYSTEM_BANK_BANK6,
				      FU_PXI_TP_REG_SYS6_SRAM_TRIGGER,
				      PXI_TP_SRAM_TRIGGER_NCS_DISABLE,
				      error))
		return FALSE;

	/* success */
	return TRUE;
}

/* ========================================================================== */
/*                             Firmware erase/CRC                             */
/* ========================================================================== */

static gboolean
fu_pxi_tp_device_firmware_clear(FuPxiTpDevice *self, FuPxiTpFirmware *firmware, GError **error)
{
	guint32 start_address = 0;

	if (firmware == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware container is NULL");
		return FALSE;
	}

	start_address = fu_pxi_tp_firmware_get_firmware_address(firmware);
	g_debug("pxi-tp: clear firmware at start address 0x%08x", start_address);

	if (!fu_pxi_tp_device_flash_erase_sector(self,
						 (guint8)(start_address / PXI_TP_SECTOR_SIZE),
						 error)) {
		g_prefix_error_literal(error, "clear firmware failure: ");
		return FALSE;
	}

	/* success */
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

	/* success */
	return TRUE;
}

static gboolean
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

	/* success */
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

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_crc_parameter(FuPxiTpDevice *self, guint32 *crc, GError **error)
{
	const guint crc_param_retry_max = 1000;
	const guint crc_param_retry_delay_ms = 10;

	guint8 out_val = 0;
	guint8 swap_flag;
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

	/* success */
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
	g_autoptr(GByteArray) page_buf = g_byte_array_new();
	gsize remain = total_sz - off;
	gsize copy_len = MIN(remain, PXI_TP_PAGE_SIZE);

	/* initialize all bytes to 0xFF */
	fu_byte_array_set_size(page_buf, PXI_TP_PAGE_SIZE, 0xFF);

	/* copy actual payload into the backing buffer */
	if (!fu_memcpy_safe(page_buf->data, /* dst */
			    page_buf->len,  /* dst_sz */
			    0,		    /* dst_off */
			    src,	    /* src */
			    total_sz,	    /* src_sz */
			    off,	    /* src_off */
			    copy_len,	    /* copy_len */
			    error)) {
		return FALSE;
	}

	/* write to SRAM using the 256-byte buffer */
	if (!fu_pxi_tp_device_write_sram_256b(self, page_buf->data, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_program_256b_to_flash(self, sector, page, error))
		return FALSE;

	/* success */
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

	g_debug("pxi-tp: update flash: size=%" G_GSIZE_FORMAT " start_sector=%u count=%u",
		total_sz,
		(guint)start_sector,
		(guint)max_sector_cnt);

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

	/* success */
	return TRUE;
}

/* ---- section processing using child-image API ---- */

static gboolean
fu_pxi_tp_device_process_section(FuPxiTpDevice *self,
				 FuPxiTpSection *section,
				 guint section_index,
				 FuProgress *prog_write,
				 guint8 start_sector,
				 guint64 *written,
				 GError **error)
{
	g_autoptr(GByteArray) data = NULL;
	FuPxiTpUpdateType update_type = FU_PXI_TP_UPDATE_TYPE_GENERAL;
	guint32 section_length;
	guint32 target_flash_start;

	g_return_val_if_fail(section != NULL, FALSE);

	update_type = fu_pxi_tp_section_get_update_type(section);
	section_length = fu_pxi_tp_section_get_section_length(section);
	target_flash_start = fu_pxi_tp_section_get_target_flash_start(section);

	data = fu_pxi_tp_section_get_payload(section, error);
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

	g_debug("pxi-tp write section %u: update_type=%u, flash=0x%08x, "
		"len=%u, sector=%u, data_len=%u",
		section_index,
		(guint)update_type,
		target_flash_start,
		(guint)section_length,
		start_sector,
		(guint)data->len);

	switch (update_type) {
	case FU_PXI_TP_UPDATE_TYPE_GENERAL:
	case FU_PXI_TP_UPDATE_TYPE_FW_SECTION:
	case FU_PXI_TP_UPDATE_TYPE_PARAM:
		if (!fu_pxi_tp_device_update_flash_process(self,
							   prog_write,
							   section_length,
							   start_sector,
							   data,
							   error)) {
			return FALSE;
		}
		*written += (guint64)section_length;
		break;

		/* TF_FORCE is now handled by the haptic child-device.
		 * It should be filtered out before calling this function.
		 */

	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unsupported update type %u for TP section %u",
			    (guint)update_type,
			    section_index);
		return FALSE;
	}

	fu_progress_step_done(prog_write);

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_write_sections(FuPxiTpDevice *self,
				const GPtrArray *sections,
				FuPxiTpFirmware *firmware,
				guint64 *written,
				FuProgress *progress,
				GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, sections->len);

	for (guint i = 0; i < sections->len; i++) {
		FuPxiTpSection *section = g_ptr_array_index((GPtrArray *)sections, i);
		guint32 section_length = 0;
		guint32 target_flash_start;
		guint8 flash_sector_start = 0;
		FuPxiTpUpdateType update_type = FU_PXI_TP_UPDATE_TYPE_GENERAL;

		/* skip non-updatable sections */
		if (!fu_pxi_tp_section_has_flag(section, FU_PXI_TP_FIRMWARE_FLAG_VALID) ||
		    fu_pxi_tp_section_has_flag(section, FU_PXI_TP_FIRMWARE_FLAG_IS_EXTERNAL)) {
			fu_progress_step_done(progress);
			continue;
		}

		update_type = fu_pxi_tp_section_get_update_type(section);

		/* skip TF_FORCE sections:
		 *   - handled by TF/haptic child device using its own image
		 *   - parent TP only handles TP firmware/parameter sections
		 */
		if (update_type == FU_PXI_TP_UPDATE_TYPE_TF_FORCE) {
			g_debug("skip TF_FORCE section %u for TP parent device", i);
			fu_progress_step_done(progress);
			continue;
		}

		section_length = fu_pxi_tp_section_get_section_length(section);
		if (section_length == 0) {
			fu_progress_step_done(progress);
			continue;
		}

		target_flash_start = fu_pxi_tp_section_get_target_flash_start(section);
		flash_sector_start = (guint8)(target_flash_start / PXI_TP_SECTOR_SIZE);

		if (!fu_pxi_tp_device_process_section(self,
						      section,
						      i,
						      progress,
						      flash_sector_start,
						      written,
						      error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_verify_crc(FuPxiTpDevice *self,
			    FuPxiTpFirmware *ctn,
			    FuProgress *progress,
			    GError **error)
{
	FuProgress *prog_verify = NULL;
	guint32 crc_value;

	prog_verify = fu_progress_get_child(progress);
	fu_progress_set_id(prog_verify, G_STRLOC);
	fu_progress_set_steps(prog_verify, 2);

	g_debug("pxi-tp: verify firmware + parameter CRC");

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

	/* success */
	return TRUE;
}

/* ========================================================================== */
/*                               Device vfuncs                                */
/* ========================================================================== */

static gboolean
fu_pxi_tp_device_setup(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 buf[2] = {0}; /* buf[0] = lo, buf[1] = hi */
	guint16 ver_u16;

	/* read low byte */
	if (!fu_pxi_tp_register_user_read(self,
					  self->ver_bank,
					  (guint8)(self->ver_addr + 0),
					  &buf[0],
					  error))
		return FALSE;

	/* read high byte */
	if (!fu_pxi_tp_register_user_read(self,
					  self->ver_bank,
					  (guint8)(self->ver_addr + 1),
					  &buf[1],
					  error))
		return FALSE;

	/* parse LE uint16 using fwupd helper */
	ver_u16 = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	g_debug("pxi-tp setup: version bytes: lo=0x%02x hi=0x%02x -> ver=0x%04x",
		buf[0],
		buf[1],
		ver_u16);

	fu_device_set_version_raw(device, ver_u16);
	/* success */
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
	guint64 total_update_bytes = 0;
	guint64 total_written_bytes = 0;

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

	/* calculate total bytes for valid internal TP sections
	 * NOTE:
	 *   - TF_FORCE sections are skipped here; they are handled by the
	 *     TF/haptic child-device using its own firmware image.
	 */
	for (guint i = 0; i < sections->len; i++) {
		FuPxiTpSection *section = g_ptr_array_index((GPtrArray *)sections, i);
		guint32 section_length = 0;
		FuPxiTpUpdateType update_type = FU_PXI_TP_UPDATE_TYPE_GENERAL;

		if (!fu_pxi_tp_section_has_flag(section, FU_PXI_TP_FIRMWARE_FLAG_VALID) ||
		    fu_pxi_tp_section_has_flag(section, FU_PXI_TP_FIRMWARE_FLAG_IS_EXTERNAL))
			continue;

		update_type = fu_pxi_tp_section_get_update_type(section);
		if (update_type == FU_PXI_TP_UPDATE_TYPE_TF_FORCE)
			continue;

		section_length = fu_pxi_tp_section_get_section_length(section);
		if (section_length > 0)
			total_update_bytes += (guint64)section_length;
	}

	if (total_update_bytes == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no internal/valid TP sections to write");
		return FALSE;
	}

	g_debug("pxi-tp: total TP update bytes=%" G_GUINT64_FORMAT, total_update_bytes);

	/* erase old firmware */
	if (!fu_pxi_tp_device_firmware_clear(self, fw_container, error))
		return FALSE;

	/* program all TP sections (TF_FORCE handled by child device) */
	if (!fu_pxi_tp_device_write_sections(self,
					     sections,
					     fw_container,
					     &total_written_bytes,
					     fu_progress_get_child(progress),
					     error))
		return FALSE;

	fu_progress_step_done(progress);

	/* verify CRC (firmware + parameter) */
	if (!fu_pxi_tp_device_verify_crc(self, fw_container, progress, error))
		return FALSE;

	fu_progress_step_done(progress);

	g_debug("pxi-tp: update success (written=%" G_GUINT64_FORMAT " / total=%" G_GUINT64_FORMAT
		")",
		total_written_bytes,
		total_update_bytes);

	/* success */
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
		return TRUE;
	}

	if (g_strcmp0(key, "PxiTpHidVersionAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xffff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ver_addr = (guint16)tmp;
		return TRUE;
	}

	if (g_strcmp0(key, "PxiTpSramSelect") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_select = (guint8)tmp;
		return TRUE;
	}

	/* new quirk: whether this TP has a TF/haptic child IC */
	if (g_strcmp0(key, "PxiTpHasTfChild") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 1, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->has_tf_child = (tmp != 0);
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

	/* nothing to do if already in application mode */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_APPLICATION, error))
		return FALSE;

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_reload(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best-effort: do not fail the whole update just because reload failed */
	if (!fu_pxi_tp_device_setup(device, &error_local)) {
		if (error_local != NULL) {
			/* single debug for ignored failure */
			g_debug("failed to refresh firmware version: %s", error_local->message);
			g_clear_error(&error_local);
		}
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	/* already in bootloader, nothing to do */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_BOOTLOADER, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_cleanup(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* ensure we are not stuck in bootloader */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pxi_tp_device_reset(self, FU_PXI_TP_RESET_MODE_APPLICATION, &error_local)) {
			if (error_local != NULL) {
				g_debug("failed to exit bootloader after update: %s",
					error_local->message);
			}
			/* still return FALSE to propagate the error */
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	return TRUE;
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

static gboolean
fu_pxi_tp_device_probe(FuDevice *device, GError **error)
{
	g_autoptr(FuPxiTpHapticDevice) child = NULL;
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (!self->has_tf_child)
		return TRUE;

	child = fu_pxi_tp_haptic_device_new(device);
	if (child == NULL) {
		g_debug("pxi-tp: failed to create TF/haptic child device");
		return TRUE;
	}

	fu_device_add_child(device, FU_DEVICE(child));

	/* success */
	return TRUE;
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

static gchar *
fu_pxi_tp_device_convert_version(FuDevice *device, guint64 version_raw)
{
	guint16 v = (guint16)version_raw; /* ensure correct width */
	return g_strdup_printf("0x%04x", v);
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

	self->has_tf_child = FALSE;
}

static void
fu_pxi_tp_device_class_init(FuPxiTpDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->probe = fu_pxi_tp_device_probe;
	device_class->setup = fu_pxi_tp_device_setup;
	device_class->write_firmware = fu_pxi_tp_device_write_firmware;
	device_class->attach = fu_pxi_tp_device_attach;
	device_class->detach = fu_pxi_tp_device_detach;
	device_class->cleanup = fu_pxi_tp_device_cleanup;
	device_class->set_progress = fu_pxi_tp_device_set_progress;
	device_class->set_quirk_kv = fu_pxi_tp_device_set_quirk_kv;
	device_class->prepare_firmware = fu_pxi_tp_device_prepare_firmware;
	device_class->convert_version = fu_pxi_tp_device_convert_version;
	device_class->reload = fu_pxi_tp_device_reload;
}
