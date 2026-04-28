/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-firmware.h"
#include "fu-pixart-tp-plp239-device.h"
#include "fu-pixart-tp-section.h"

struct _FuPixartTpPlp239Device {
	FuPixartTpDevice parent_instance;
};

G_DEFINE_TYPE(FuPixartTpPlp239Device, fu_pixart_tp_plp239_device, FU_TYPE_PIXART_TP_DEVICE)

#define FU_PIXART_TP_DEVICE_SECTOR_SIZE 4096
#define FU_PIXART_TP_DEVICE_PAGE_SIZE	256

static gboolean
fu_pixart_tp_plp239_device_set_flash_command(FuPixartTpPlp239Device *self,
					     FuPixartTpSfcCommandPlp239 command,
					     GError **error)
{
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_PLP239_SFC_COMMAND,
						command,
						error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_get_flash_controller_status(
    FuPixartTpPlp239Device *self,
    FuPixartTpFlashControllerStatusPlp239 *status,
    GError **error)
{
	guint8 flash_status = 0;
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK6,
					       FU_PIXART_TP_REG_SYS6_PLP239_FLASH_CONTROLLER_STATUS,
					       &flash_status,
					       error))
		return FALSE;

	*status = (FuPixartTpFlashControllerStatusPlp239)flash_status;
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_reset_sram_offset(FuPixartTpPlp239Device *self, GError **error)
{
	guint16 sram_address = 0x0000;
	if (!fu_pixart_tp_device_register_write_uint16(FU_PIXART_TP_DEVICE(self),
						       FU_PIXART_TP_SYSTEM_BANK_BANK4,
						       FU_PIXART_TP_REG_SYS4_PLP239_SRAM_OFFSET,
						       sram_address,
						       error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_flash_command_finish(FuPixartTpPlp239Device *self, GError **error)
{
	FuPixartTpFlashControllerStatusPlp239 flash_status;
	/* execute finish command */
	if (!fu_pixart_tp_plp239_device_set_flash_command(self,
							  FU_PIXART_TP_SFC_COMMAND_PLP239_FINISH,
							  error))
		return FALSE;

	if (!fu_pixart_tp_plp239_device_get_flash_controller_status(self, &flash_status, error))
		return FALSE;

	if (((flash_status & FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_HIGH_LEVEL_ENABLE) != 0) ||
	    ((flash_status & FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_LEVEL1_ENABLE) != 0)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "cannot disable flash controller");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_lock_low_level_protection(FuPixartTpPlp239Device *self, GError **error)
{
	/* lock low level protection */
	return fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						  FU_PIXART_TP_SYSTEM_BANK_BANK4,
						  FU_PIXART_TP_REG_SYS4_PLP239_LOW_LEVEL_PROTECTION,
						  FU_PIXART_TP_LOW_LEVEL_PROTECTION_KEY_PLP239_LOCK,
						  error);
}

static gboolean
fu_pixart_tp_plp239_device_unlock_low_level_protection(FuPixartTpPlp239Device *self, GError **error)
{
	/* unlock low level protection */
	return fu_pixart_tp_device_register_write(
	    FU_PIXART_TP_DEVICE(self),
	    FU_PIXART_TP_SYSTEM_BANK_BANK4,
	    FU_PIXART_TP_REG_SYS4_PLP239_LOW_LEVEL_PROTECTION,
	    FU_PIXART_TP_LOW_LEVEL_PROTECTION_KEY_PLP239_UNLOCK,
	    error);
}

static gboolean
fu_pixart_tp_plp239_device_unlock_level_zero_protection(FuPixartTpPlp239Device *self,
							GError **error)
{
	FuPixartTpFlashControllerStatusPlp239 flash_status;

	/* unlock level zero protection */
	if (!fu_pixart_tp_device_register_write(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK6,
		FU_PIXART_TP_REG_SYS6_PLP239_ZERO_LEVEL_PROTECT_KEY,
		FU_PIXART_TP_ZERO_LEVEL_PROTECT_KEY_PLP239_PROTECT_KEY,
		error))
		return FALSE;

	if (!fu_pixart_tp_plp239_device_get_flash_controller_status(self, &flash_status, error))
		return FALSE;

	if ((flash_status & FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_HIGH_LEVEL_ENABLE) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "can not unlock level 0 protection");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_unlock_level_one_protection(FuPixartTpPlp239Device *self, GError **error)
{
	FuPixartTpFlashControllerStatusPlp239 flash_status;

	/* unlock level one protection */
	if (!fu_pixart_tp_device_register_write(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK6,
		FU_PIXART_TP_REG_SYS6_PLP239_ONE_LEVEL_PROTECT_KEY,
		FU_PIXART_TP_ONE_LEVEL_PROTECT_KEY_PLP239_PROTECT_KEY,
		error))
		return FALSE;

	if (!fu_pixart_tp_plp239_device_get_flash_controller_status(self, &flash_status, error))
		return FALSE;

	if ((flash_status & FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_LEVEL1_ENABLE) !=
	    FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_LEVEL1_ENABLE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "can not unlock level 1 protection");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_set_flash_sector_address(FuPixartTpPlp239Device *self,
						    guint8 sector,
						    guint8 sector_length,
						    GError **error)
{
	/* write flash sector address*/
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_PLP239_FLASH_SECTOR_ADDRESS,
						sector,
						error))
		return FALSE;

	/* write flash sector length*/
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_PLP239_FLASH_SECTOR_LENGTH,
						sector_length,
						error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_disable_cpu_access(FuPixartTpPlp239Device *self, GError **error)
{
	/* disable cpu access */
	return fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						  FU_PIXART_TP_SYSTEM_BANK_BANK4,
						  FU_PIXART_TP_REG_SYS4_PLP239_MASK_CPU_ACCESS,
						  FU_PIXART_TP_CPU_ACCESS_MASK_PLP239_DISABLE,
						  error);
}

static gboolean
fu_pixart_tp_plp239_device_enable_watchdog(FuPixartTpPlp239Device *self,
					   gboolean enable,
					   GError **error)
{
	guint8 val = enable ? FU_PIXART_TP_WATCH_DOG_KEY_PLP239_ENABLE
			    : FU_PIXART_TP_WATCH_DOG_KEY_PLP239_DISABLE;

	/* set watchdog state */
	return fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						  FU_PIXART_TP_SYSTEM_BANK_BANK6,
						  FU_PIXART_TP_REG_SYS6_PLP239_WATCHDOG_DISABLE,
						  val,
						  error);
}

static gboolean
fu_pixart_tp_plp239_device_disable_cpu_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);
	guint8 clock_pu = 0;
	guint8 clock_pd = 0;

	/* write protect key to allow clock changes */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK0,
						FU_PIXART_TP_REG_SYS0_PLP239_BANK0_PROTECT_KEY,
						FU_PIXART_TP_REG_SYS0_PLP239_BANK0_PROTECT_KEY,
						error))
		return FALSE;

	/* set clock power down/up bits */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK0,
						FU_PIXART_TP_REG_SYS0_PLP239_CLOCK_PD,
						FU_PIXART_TP_CLOCKS_POWER_DISABLE_PLP239_CPU,
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK0,
						FU_PIXART_TP_REG_SYS0_PLP239_CLOCK_PU2,
						FU_PIXART_TP_CLOCKS_POWER_UP_NYQ |
						    FU_PIXART_TP_CLOCKS_POWER_UP_NYQ_F,
						error))
		return FALSE;

	/* verify status */
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK0,
					       FU_PIXART_TP_REG_SYS0_PLP239_CLOCK_PU2,
					       &clock_pu,
					       error))
		return FALSE;
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK0,
					       FU_PIXART_TP_REG_SYS0_PLP239_CLOCK_PD,
					       &clock_pd,
					       error))
		return FALSE;

	if (clock_pu != (FU_PIXART_TP_CLOCKS_POWER_UP_NYQ | FU_PIXART_TP_CLOCKS_POWER_UP_NYQ_F) ||
	    clock_pd != FU_PIXART_TP_CLOCKS_POWER_DISABLE_PLP239_CPU) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "clock state mismatch");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_enable_cpu(FuPixartTpPlp239Device *self, gboolean enable, GError **error)
{
	/* enable cpu clock */
	if (enable) {
		if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
							FU_PIXART_TP_SYSTEM_BANK_BANK0,
							FU_PIXART_TP_REG_SYS0_PLP239_CLOCK_PU,
							FU_PIXART_TP_CLOCKS_POWER_UP_CPU,
							error))
			return FALSE;
		return fu_pixart_tp_device_register_write(
		    FU_PIXART_TP_DEVICE(self),
		    FU_PIXART_TP_SYSTEM_BANK_BANK0,
		    FU_PIXART_TP_REG_SYS0_PLP239_CLOCK_PD,
		    FU_PIXART_TP_CLOCKS_POWER_DISABLE_PLP239_NONE,
		    error);
	}

	/* disable cpu clock with retry */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_plp239_device_disable_cpu_cb,
				  10,	/* count */
				  0,	/* delay ms */
				  NULL, /* user_data */
				  error)) {
		g_prefix_error_literal(error, "failed to disable cpu: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_check_nav_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);
	guint8 boot_status = 0;
	/* sometimes the boot status will get a 0xff value, which is a FW not ready */
	guint8 busy_mask = 0xff;

	/* check if nav is ready */
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK6,
					       FU_PIXART_TP_REG_SYS6_PLP239_BOOT_STATUS,
					       &boot_status,
					       error))
		return FALSE;

	if ((boot_status & FU_PIXART_TP_BOOT_STATUS_PLP239_NAV_READY) != 0 &&
	    boot_status != busy_mask)
		return TRUE;

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "navigation is not ready yet");
	return FALSE;
}

static gboolean
fu_pixart_tp_plp239_device_check_device_boot_status(FuPixartTpPlp239Device *self, GError **error)
{
	guint8 boot_status = 0;
	g_autoptr(GError) error_local = NULL;

	/* check nav_ready with 50 retries and 10ms delay */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_plp239_device_check_nav_ready_cb,
				  50,	/* count */
				  10,	/* delay ms */
				  NULL, /* user_data */
				  &error_local)) {
		g_debug("ignoring: %s", error_local->message);
	} else {
		return TRUE;
	}

	/* read status to determine exact failure reason */
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK6,
					       FU_PIXART_TP_REG_SYS6_PLP239_BOOT_STATUS,
					       &boot_status,
					       error)) {
		return FALSE;
	}

	/* verify hardware and firmware checks */
	if ((boot_status & FU_PIXART_TP_BOOT_STATUS_PLP239_HW_READY) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "hardware is not ready");
		return FALSE;
	}
	if ((boot_status & FU_PIXART_TP_BOOT_STATUS_PLP239_FW_CODE_PASS) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware code checking did not pass");
		return FALSE;
	}
	if ((boot_status & FU_PIXART_TP_BOOT_STATUS_PLP239_IFB_CHECK_SUM_PASS) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "information block checksum failed");
		return FALSE;
	}
	if ((boot_status & FU_PIXART_TP_BOOT_STATUS_PLP239_WDOG) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "watchdog was signaled");
		return FALSE;
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "unknown boot status error");
	return FALSE;
}

static gboolean
fu_pixart_tp_plp239_device_clear_hid_firmware_ready(FuPixartTpPlp239Device *self, GError **error)
{
	/* clear hid firmware ready signal */
	return fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						  FU_PIXART_TP_SYSTEM_BANK_BANK8,
						  FU_PIXART_TP_REG_SYS8_PLP239_HID_FIRMWARE_READY,
						  FU_PIXART_TP_HID_FIRMWARE_READY_PLP239_NONE,
						  error);
}

static gboolean
fu_pixart_tp_plp239_device_flash_rstqio_step(FuPixartTpPlp239Device *self,
					     guint8 quad_mode,
					     GError **error)
{
	/* set flash quad mode */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_PLP239_FLASH_QUAD,
						quad_mode,
						error))
		return FALSE;

	/* reset flash quad I/O, EoN */
	if (!fu_pixart_tp_device_register_write(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_COMMAND,
		FU_PIXART_TP_FLASH_COMMAND_PLP239_FLASH_CMD_RSTQIO_EON,
		error))
		return FALSE;

	/* set flash data count to 0 */
	if (!fu_pixart_tp_device_register_write_uint16(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_DATA_COUNT,
		0x0000,
		error))
		return FALSE;

	/* flash write instruction */
	return fu_pixart_tp_device_register_write(
	    FU_PIXART_TP_DEVICE(self),
	    FU_PIXART_TP_SYSTEM_BANK_BANK4,
	    FU_PIXART_TP_REG_SYS4_PLP239_FLASH_INSTRUCTION,
	    FU_PIXART_TP_FLASH_INST_PLP239_FLASH_WRITE_INSTRUCTION,
	    error);
}

static gboolean
fu_pixart_tp_plp239_device_release_flash_quad_enhance_mode(FuPixartTpPlp239Device *self,
							   GError **error)
{
	guint32 addr = 0;
	guint8 q_mode_en = FU_PIXART_TP_FLASH_QUAD_EQIO_EON | FU_PIXART_TP_FLASH_QUAD_QUAD_MD;

	/* flash quad mode and enhance performance for read data */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_PLP239_FLASH_QUAD,
						FU_PIXART_TP_FLASH_QUAD_QUAD_ENPERF_MD,
						error))
		return FALSE;

	/* set flash data count to 1 */
	if (!fu_pixart_tp_device_register_write_uint16(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_DATA_COUNT,
		0x0001,
		error))
		return FALSE;

	/* set flash address to 0*/
	if (!fu_pixart_tp_device_register_write_uint24(FU_PIXART_TP_DEVICE(self),
						       FU_PIXART_TP_SYSTEM_BANK_BANK4,
						       FU_PIXART_TP_REG_SYS4_PLP239_FLASH_ADDRESS,
						       addr,
						       error))
		return FALSE;

	/* toggle dummy bits to escape performance enhance mode */
	if (!fu_pixart_tp_device_register_write_uint24(FU_PIXART_TP_DEVICE(self),
						       FU_PIXART_TP_SYSTEM_BANK_BANK4,
						       FU_PIXART_TP_REG_SYS4_PLP239_DUMMY_BY,
						       0,
						       error))
		return FALSE;

	/* flash read data */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_PLP239_FLASH_INSTRUCTION,
						FU_PIXART_TP_FLASH_INST_PLP239_FLASH_READ_DATA,
						error))
		return FALSE;

	/* IMPORTANT: this sequence is a mandatory hardware workaround (errata)
	 * do not refactor or remove this redundant flow, as it is required for ic stability */
	if (!fu_pixart_tp_plp239_device_flash_rstqio_step(self, q_mode_en, error))
		return FALSE; /* retry 1 */
	if (!fu_pixart_tp_plp239_device_flash_rstqio_step(self,
							  FU_PIXART_TP_FLASH_QUAD_NONE,
							  error))
		return FALSE; /* turn off */
	if (!fu_pixart_tp_plp239_device_flash_rstqio_step(self, q_mode_en, error))
		return FALSE; /* retry 2 */
	if (!fu_pixart_tp_plp239_device_flash_rstqio_step(self, q_mode_en, error))
		return FALSE; /* retry 3 */
	if (!fu_pixart_tp_plp239_device_flash_rstqio_step(self,
							  FU_PIXART_TP_FLASH_QUAD_NONE,
							  error))
		return FALSE; /* turn off */

	/* clear flash quad mode setting */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_PLP239_FLASH_QUAD,
						FU_PIXART_TP_FLASH_QUAD_NONE,
						error))
		return FALSE;

	/* set flash command to WRSR (Write Status, EoN/Winbond) */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_PLP239_FLASH_COMMAND,
						FU_PIXART_TP_FLASH_COMMAND_PLP239_FLASH_CMD_WRSR,
						error))
		return FALSE;

	/* set flash data count to 1 */
	if (!fu_pixart_tp_device_register_write_uint16(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_DATA_COUNT,
		0x0001,
		error))
		return FALSE;

	/* write data word 0 to flash*/
	if (!fu_pixart_tp_device_register_write_uint32(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_WRITE_DATA_WORD0,
		0,
		error))
		return FALSE;

	/* flash write instruction */
	if (!fu_pixart_tp_device_register_write(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_INSTRUCTION,
		FU_PIXART_TP_FLASH_INST_PLP239_FLASH_WRITE_INSTRUCTION,
		error))
		return FALSE;

	/* release power down */
	if (!fu_pixart_tp_device_register_write(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_COMMAND,
		FU_PIXART_TP_FLASH_COMMAND_PLP239_FLASH_CMD_RELEASE_POWER_DOWN,
		error))
		return FALSE;

	/* set flash data count to 1 */
	if (!fu_pixart_tp_device_register_write_uint16(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK4,
		FU_PIXART_TP_REG_SYS4_PLP239_FLASH_DATA_COUNT,
		0x0001,
		error))
		return FALSE;

	/* set flash address to 0*/
	if (!fu_pixart_tp_device_register_write_uint24(FU_PIXART_TP_DEVICE(self),
						       FU_PIXART_TP_SYSTEM_BANK_BANK4,
						       FU_PIXART_TP_REG_SYS4_PLP239_FLASH_ADDRESS,
						       addr,
						       error))
		return FALSE;

	/* flash read data */
	return fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						  FU_PIXART_TP_SYSTEM_BANK_BANK4,
						  FU_PIXART_TP_REG_SYS4_PLP239_FLASH_INSTRUCTION,
						  FU_PIXART_TP_FLASH_INST_PLP239_FLASH_READ_DATA,
						  error);
}

static gboolean
fu_pixart_tp_plp239_device_wait_hardware_done_cb(FuDevice *device,
						 gpointer user_data,
						 GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);
	FuPixartTpFlashControllerStatusPlp239 status;

	if (!fu_pixart_tp_plp239_device_get_flash_controller_status(self, &status, error))
		return FALSE;

	if ((status & FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_FINISH) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "hardware operation is not finished yet");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_get_crc_firmware(FuPixartTpPlp239Device *self,
					    guint32 *crc,
					    GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	*crc = 0;

	/* read crc result (32-bit LE) */
	buf = fu_pixart_tp_device_register_read_array(FU_PIXART_TP_DEVICE(self),
						      FU_PIXART_TP_SYSTEM_BANK_BANK6,
						      FU_PIXART_TP_REG_SYS6_PLP239_FIRMWARE_CRC,
						      4,
						      error);
	if (buf == NULL)
		return FALSE;

	/* safely read 32-bit CRC with bounds checking */
	if (!fu_memread_uint32_safe(buf->data, buf->len, 0x0, crc, G_LITTLE_ENDIAN, error))
		return FALSE;

	g_debug("firmware CRC: 0x%08x", *crc);

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_get_crc_parameter(FuPixartTpPlp239Device *self,
					     guint32 *crc,
					     GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	*crc = 0;

	/* read crc result (32-bit LE) */
	buf = fu_pixart_tp_device_register_read_array(FU_PIXART_TP_DEVICE(self),
						      FU_PIXART_TP_SYSTEM_BANK_BANK6,
						      FU_PIXART_TP_REG_SYS6_PLP239_PARAMETER_CRC,
						      4,
						      error);
	if (buf == NULL)
		return FALSE;

	/* safely read 32-bit CRC with bounds checking */
	if (!fu_memread_uint32_safe(buf->data, buf->len, 0x0, crc, G_LITTLE_ENDIAN, error))
		return FALSE;

	g_debug("parameter CRC: 0x%08x", *crc);

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_get_crc_hid_descriptor(FuPixartTpPlp239Device *self,
						  guint32 *crc,
						  GError **error)
{
	guint8 crc_ctrl = FU_PIXART_TP_CRC_CTRL_HID_DESCRIPTOR;
	g_autoptr(GByteArray) buf = NULL;

	*crc = 0;

	/* write crc trigger */
	if (!fu_pixart_tp_device_register_write(
		FU_PIXART_TP_DEVICE(self),
		FU_PIXART_TP_SYSTEM_BANK_BANK6,
		FU_PIXART_TP_REG_SYS6_PLP239_HID_DESCRIPTOR_CRC_CTRL,
		crc_ctrl,
		error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 250);

	/* read crc result (32-bit LE) */
	buf =
	    fu_pixart_tp_device_register_read_array(FU_PIXART_TP_DEVICE(self),
						    FU_PIXART_TP_SYSTEM_BANK_BANK6,
						    FU_PIXART_TP_REG_SYS6_PLP239_HID_DESCRIPTOR_CRC,
						    4,
						    error);
	if (buf == NULL)
		return FALSE;

	/* safely read 32-bit CRC with bounds checking */
	if (!fu_memread_uint32_safe(buf->data, buf->len, 0x0, crc, G_LITTLE_ENDIAN, error))
		return FALSE;

	g_debug("hid CRC: 0x%08x", *crc);

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_flash_program_4096b_to_flash(FuPixartTpPlp239Device *self,
							guint8 sector,
							GError **error)
{
	guint8 enable_bist = 0x01;

	/* reset sram offset to zero */
	if (!fu_pixart_tp_plp239_device_reset_sram_offset(self, error))
		return FALSE;

	/* unlock level zero protection*/
	if (!fu_pixart_tp_plp239_device_unlock_level_zero_protection(self, error))
		return FALSE;

	/* unlock level one protection */
	if (!fu_pixart_tp_plp239_device_unlock_level_one_protection(self, error))
		return FALSE;

	/* enable BIST */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_PLP239_PROGRAM_BIST,
						enable_bist,
						error))
		return FALSE;

	/* set flash sector address */
	if (!fu_pixart_tp_plp239_device_set_flash_sector_address(
		self,
		sector,
		FU_PIXART_TP_FLASH_SECTOR_LENGTH_PLP239_KB_4,
		error))
		return FALSE;

	/* set program flash command */
	if (!fu_pixart_tp_plp239_device_set_flash_command(self,
							  FU_PIXART_TP_SFC_COMMAND_PLP239_PROGRAM,
							  error))
		return FALSE;

	/* wait hardware done */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_plp239_device_wait_hardware_done_cb,
				  8000, /* count */
				  0,	/* delay ms */
				  NULL, /* user_data */
				  error)) {
		g_prefix_error_literal(
		    error,
		    "failed to wait for hardware done after dump flash to sram: ");
		return FALSE;
	}

	/* finish program */
	if (!fu_pixart_tp_plp239_device_flash_command_finish(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_flash_dump_4096b_from_flash_to_sram(FuPixartTpPlp239Device *self,
							       guint8 sector,
							       GError **error)
{
	if (!fu_pixart_tp_plp239_device_unlock_level_zero_protection(self, error))
		return FALSE;

	/* set flash read address */
	if (!fu_pixart_tp_plp239_device_set_flash_sector_address(
		self,
		sector,
		FU_PIXART_TP_FLASH_SECTOR_LENGTH_PLP239_KB_4,
		error))
		return FALSE;

	/* set read flash command */
	if (!fu_pixart_tp_plp239_device_set_flash_command(self,
							  FU_PIXART_TP_SFC_COMMAND_PLP239_READ,
							  error))
		return FALSE;

	/* wait hardware done */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_plp239_device_wait_hardware_done_cb,
				  8000, /* count */
				  0,	/* delay ms */
				  NULL, /* user_data */
				  error)) {
		g_prefix_error_literal(
		    error,
		    "failed to wait for hardware done after dump flash to sram: ");
		return FALSE;
	}

	/* finish read */
	if (!fu_pixart_tp_plp239_device_flash_command_finish(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_write_sram_4096b(FuPixartTpPlp239Device *self,
					    const guint8 *data,
					    GError **error)
{
	guint8 dummy = 0x00;
	guint8 work_around_mask = 0x10;
	FuPixartTpFlashControllerStatusPlp239 flash_status;
	if (!fu_pixart_tp_plp239_device_unlock_level_zero_protection(self, error))
		return FALSE;

	if (!fu_pixart_tp_plp239_device_set_flash_command(
		self,
		FU_PIXART_TP_SFC_COMMAND_PLP239_SRAM_READ_WRITE,
		error))
		return FALSE;

	/* * WORKAROUND: Force the register pointer to Bank 6, 0x25 before burst read/write.
	 * Applying bitwise OR with 0x10 instructs(read operation) the IC to only set the address
	 * focus without executing an actual data write.
	 */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK6 | work_around_mask,
						FU_PIXART_TP_REG_SYS6_PLP239_SRAM_ACCESS_DATA,
						dummy,
						error))
		return FALSE;

	/* burst write data*/
	if (!fu_pixart_tp_device_register_burst_write(FU_PIXART_TP_DEVICE(self),
						      data,
						      FU_PIXART_TP_DEVICE_SECTOR_SIZE,
						      error)) {
		g_prefix_error_literal(error, "burst write buffer failure: ");
		return FALSE;
	}

	if (!fu_pixart_tp_plp239_device_get_flash_controller_status(self, &flash_status, error))
		return FALSE;

	if ((flash_status & FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_BUFFER_OVER_FLOW) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "sram access overflow");
		return FALSE;
	}

	/* write finish */
	if (!fu_pixart_tp_plp239_device_flash_command_finish(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_read_sram_4096b(FuPixartTpPlp239Device *self,
					   guint8 *data,
					   GError **error)
{
	guint8 dummy = 0x00;
	guint8 work_around_mask = 0x10;
	FuPixartTpFlashControllerStatusPlp239 flash_status;

	if (!fu_pixart_tp_plp239_device_unlock_level_zero_protection(self, error))
		return FALSE;

	if (!fu_pixart_tp_plp239_device_set_flash_command(
		self,
		FU_PIXART_TP_SFC_COMMAND_PLP239_SRAM_READ_WRITE,
		error))
		return FALSE;

	/* * WORKAROUND: Force the register pointer to Bank 6, 0x25 before burst read/write.
	 * Applying bitwise OR with 0x10 instructs(read operation) the IC to only set the address
	 * focus without executing an actual data write.
	 */
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK6 | work_around_mask,
						FU_PIXART_TP_REG_SYS6_PLP239_SRAM_ACCESS_DATA,
						dummy,
						error))
		return FALSE;

	/* burst read data*/
	if (!fu_pixart_tp_device_register_burst_read(FU_PIXART_TP_DEVICE(self),
						     data,
						     FU_PIXART_TP_DEVICE_SECTOR_SIZE,
						     error)) {
		g_prefix_error_literal(error, "burst read buffer failure: ");
		return FALSE;
	}

	if (!fu_pixart_tp_plp239_device_get_flash_controller_status(self, &flash_status, error))
		return FALSE;

	if ((flash_status & FU_PIXART_TP_FLASH_CONTROLLER_STATUS_PLP239_BUFFER_OVER_FLOW) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "sram access overflow");
		return FALSE;
	}

	/* write finish */
	if (!fu_pixart_tp_plp239_device_flash_command_finish(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_write_sector(FuPixartTpPlp239Device *self,
					guint8 sector,
					FuChunk *chk,
					GError **error)
{
	g_autoptr(GByteArray) sector_buf = g_byte_array_new();
	g_autoptr(GBytes) blob = fu_chunk_get_bytes(chk);

	/* initialize all extra bytes to 0xFF */
	fu_byte_array_append_bytes(sector_buf, blob);
	fu_byte_array_set_size(sector_buf, FU_PIXART_TP_DEVICE_SECTOR_SIZE, 0xFF);

	/* write to SRAM using the 4096-byte buffer */
	if (!fu_pixart_tp_plp239_device_write_sram_4096b(self, sector_buf->data, error))
		return FALSE;

	/* program SRAM 4096byte to flash */
	if (!fu_pixart_tp_plp239_device_flash_program_4096b_to_flash(self, sector, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_read_sector(FuPixartTpPlp239Device *self,
				       guint8 sector,
				       guint8 *data,
				       GError **error)
{
	/* dump flash to sram */
	if (!fu_pixart_tp_plp239_device_flash_dump_4096b_from_flash_to_sram(self, sector, error))
		return FALSE;

	/* read sram */
	if (!fu_pixart_tp_plp239_device_read_sram_4096b(self, data, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_reset(FuPixartTpPlp239Device *self,
				 FuPixartTpResetMode mode,
				 GError **error)
{
	if (mode == FU_PIXART_TP_RESET_MODE_APPLICATION) {
		if (!fu_pixart_tp_plp239_device_lock_low_level_protection(self, error))
			return FALSE;

		if (!fu_pixart_tp_plp239_device_disable_cpu_access(self, error))
			return FALSE;

		if (!fu_pixart_tp_plp239_device_enable_watchdog(self, TRUE, error))
			return FALSE;

		/* write reset keys */
		if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
							FU_PIXART_TP_SYSTEM_BANK_BANK1,
							FU_PIXART_TP_REG_SYS1_PLP239_RESET_KEY1,
							FU_PIXART_TP_RESET_KEY1_SUSPEND,
							error))
			return FALSE;
		if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
							FU_PIXART_TP_SYSTEM_BANK_BANK1,
							FU_PIXART_TP_REG_SYS1_PLP239_RESET_KEY2,
							FU_PIXART_TP_RESET_KEY2_REGULAR,
							error))
			return FALSE;

		fu_device_sleep(FU_DEVICE(self), 50);
		if (!fu_pixart_tp_plp239_device_enable_cpu(self, TRUE, error))
			return FALSE;

		fu_device_sleep(FU_DEVICE(self), 20);
		if (!fu_pixart_tp_plp239_device_check_device_boot_status(self, error))
			return FALSE;

	} else {
		if (!fu_pixart_tp_plp239_device_clear_hid_firmware_ready(self, error))
			return FALSE;

		fu_device_sleep(FU_DEVICE(self), 200);

		if (!fu_pixart_tp_plp239_device_enable_watchdog(self, FALSE, error))
			return FALSE;
		if (!fu_pixart_tp_plp239_device_enable_cpu(self, FALSE, error))
			return FALSE;
		if (!fu_pixart_tp_plp239_device_unlock_low_level_protection(self, error))
			return FALSE;
		if (!fu_pixart_tp_plp239_device_disable_cpu_access(self, error))
			return FALSE;
		if (!fu_pixart_tp_plp239_device_release_flash_quad_enhance_mode(self, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_flash_erase_sector(FuPixartTpPlp239Device *self,
					      guint erase_sector,
					      GError **error)
{
	/* sanity check */
	if (erase_sector > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "sector 0x%x exceeds maximum",
			    erase_sector);
		return FALSE;
	}

	if (!fu_pixart_tp_plp239_device_unlock_level_zero_protection(self, error))
		return FALSE;
	if (!fu_pixart_tp_plp239_device_unlock_level_one_protection(self, error))
		return FALSE;

	/* set erased flash sector address */
	if (!fu_pixart_tp_plp239_device_set_flash_sector_address(
		self,
		erase_sector,
		FU_PIXART_TP_FLASH_SECTOR_LENGTH_PLP239_KB_4,
		error))
		return FALSE;

	g_debug("erase sector %u", (guint)erase_sector);

	/* execute erase command */
	if (!fu_pixart_tp_plp239_device_set_flash_command(self,
							  FU_PIXART_TP_SFC_COMMAND_PLP239_ERASE,
							  error))
		return FALSE;

	/* wait hardware done */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_plp239_device_wait_hardware_done_cb,
				  8000, /* count */
				  0,	/* delay ms */
				  NULL, /* user_data */
				  error)) {
		g_prefix_error_literal(error,
				       "failed to wait for hardware done after flash erase: ");
		return FALSE;
	}

	/* finish erase */
	if (!fu_pixart_tp_plp239_device_flash_command_finish(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_verify_extend_flash(FuPixartTpPlp239Device *self,
					       guint8 sector,
					       FuChunk *chk,
					       GError **error)
{
	guint8 read_buf[FU_PIXART_TP_DEVICE_SECTOR_SIZE] = {0};
	gsize blob_sz = 0;
	const guint8 *blob_data;
	gboolean verify_ok;
	g_autoptr(GBytes) blob = fu_chunk_get_bytes(chk);

	blob_data = g_bytes_get_data(blob, &blob_sz);

	/* read back the sector to sram and then to our buffer */
	if (!fu_pixart_tp_plp239_device_read_sector(self, sector, read_buf, error)) {
		g_prefix_error(error,
			       "failed to read back sector 0x%x for verification: ",
			       (guint)sector);
		return FALSE;
	}

	/* safely compare the actual payload with bounds checking */
	verify_ok =
	    fu_memcmp_safe(read_buf, sizeof(read_buf), 0x0, blob_data, blob_sz, 0x0, blob_sz, NULL);

	/* assert that the remaining padded region is all 0xFF */
	if (verify_ok) {
		for (gsize j = blob_sz; j < FU_PIXART_TP_DEVICE_SECTOR_SIZE; j++) {
			if (read_buf[j] != 0xFF) {
				verify_ok = FALSE;
				break;
			}
		}
	}

	if (!verify_ok) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "extend flash verification failed at sector 0x%x",
			    (guint)sector);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_write_section(FuPixartTpPlp239Device *self,
					 FuPixartTpSection *section,
					 FuProgress *progress,
					 GError **error)
{
	/* normal fw: sector 0 ~ 8 (9 sectors)
	** extend fw: sector 9 ~ 10 (2 sectors)
	*/
	const guint8 extend_flash_sector = 9;
	FuPixartTpUpdateType update_type = FU_PIXART_TP_UPDATE_TYPE_GENERAL;
	guint32 target_flash_start = fu_pixart_tp_section_get_target_flash_start(section);
	guint start_sector;
	guint sector_count;
	guint first_idx;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* nothing to do */
	if (fu_firmware_get_size(FU_FIRMWARE(section)) == 0)
		return TRUE;

	update_type = fu_pixart_tp_section_get_update_type(section);
	if (update_type != FU_PIXART_TP_UPDATE_TYPE_GENERAL &&
	    update_type != FU_PIXART_TP_UPDATE_TYPE_FW_SECTION &&
	    update_type != FU_PIXART_TP_UPDATE_TYPE_PARAM &&
	    update_type != FU_PIXART_TP_UPDATE_TYPE_HID_DESCRIPTOR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unsupported update type %u for TP section",
			    (guint)update_type);
		return FALSE;
	}

	/* chunk section data into sectors */
	stream = fu_firmware_get_stream(FU_FIRMWARE(section), error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						0x0,
						0x0,
						FU_PIXART_TP_DEVICE_SECTOR_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	sector_count = fu_chunk_array_length(chunks);
	if (sector_count == 0)
		return TRUE;

	/* progress: 2 steps per sector (erase + program) */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, sector_count * 2);

	start_sector = target_flash_start / FU_PIXART_TP_DEVICE_SECTOR_SIZE;

	/* keep sector 0 valid until the end */
	first_idx = (start_sector == 0) ? 1 : 0;

	/* erase phase: erase remaining sectors first in physical order */
	for (guint i = first_idx; i < sector_count; i++) {
		if (!fu_pixart_tp_plp239_device_flash_erase_sector(self, start_sector + i, error)) {
			g_prefix_error(error, "failed to erase sector 0x%x: ", start_sector + i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* write phase: write remaining sectors first */
	for (guint i = first_idx; i < sector_count; i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* write chunk to the corresponding flash sector */
		if (!fu_pixart_tp_plp239_device_write_sector(self,
							     (guint8)(start_sector + i),
							     chk,
							     error)) {
			g_prefix_error(error,
				       "failed to write sector 0x%x: ",
				       (guint8)(start_sector + i));
			return FALSE;
		}

		/* workaround: for the plp239, there is an extendflash block that is not included in
		 * the crc check. if the burning process is incomplete, this block may not be
		 * detected by the crc verification. we need read flash back to verify manually */
		if (start_sector == 0 && i >= extend_flash_sector) {
			if (!fu_pixart_tp_plp239_device_verify_extend_flash(
				self,
				(guint8)(start_sector + i),
				chk,
				error))
				return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* final phase: erase + write sector 0 last */
	if (start_sector == 0 && sector_count > 0) {
		g_autoptr(FuChunk) chk_0 = NULL;

		/* erase sector 0 */
		if (!fu_pixart_tp_plp239_device_flash_erase_sector(self, 0, error)) {
			g_prefix_error_literal(error, "failed to erase sector 0x0: ");
			return FALSE;
		}
		fu_progress_step_done(progress);

		/* write sector 0 */
		chk_0 = fu_chunk_array_index(chunks, 0, error);
		if (chk_0 == NULL)
			return FALSE;

		if (!fu_pixart_tp_plp239_device_write_sector(self, 0, chk_0, error)) {
			g_prefix_error_literal(error, "failed to write sector 0x0: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_verify_crc(FuPixartTpPlp239Device *self,
				      FuPixartTpFirmware *firmware,
				      FuProgress *progress,
				      GError **error)
{
	guint32 crc_value;
	FuPixartTpSection *section;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 49, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 8, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 43, NULL);

	/* important: 239 device must reset to application to recalculate the CRC, not reset to
	 * bootloader */
	if (!fu_pixart_tp_plp239_device_reset(self, FU_PIXART_TP_RESET_MODE_APPLICATION, error))
		return FALSE;

	/* firmware CRC */
	if (!fu_pixart_tp_plp239_device_get_crc_firmware(self, &crc_value, error))
		return FALSE;
	section = fu_pixart_tp_firmware_find_section_by_type(firmware,
							     FU_PIXART_TP_UPDATE_TYPE_FW_SECTION,
							     error);
	if (section == NULL)
		return FALSE;
	if (crc_value != fu_pixart_tp_section_get_crc(section)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware CRC compare failed");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* parameter CRC */
	if (!fu_pixart_tp_plp239_device_get_crc_parameter(self, &crc_value, error))
		return FALSE;
	section = fu_pixart_tp_firmware_find_section_by_type(firmware,
							     FU_PIXART_TP_UPDATE_TYPE_PARAM,
							     error);
	if (section == NULL)
		return FALSE;
	if (crc_value != fu_pixart_tp_section_get_crc(section)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "parameter CRC compare failed");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* hid descriptor CRC*/
	if (!fu_pixart_tp_plp239_device_get_crc_hid_descriptor(self, &crc_value, error))
		return FALSE;
	section =
	    fu_pixart_tp_firmware_find_section_by_type(firmware,
						       FU_PIXART_TP_UPDATE_TYPE_HID_DESCRIPTOR,
						       error);
	if (section == NULL)
		return FALSE;
	if (crc_value != fu_pixart_tp_section_get_crc(section)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "hid CRC compare failed");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_setup(FuDevice *device, GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);
	guint8 ver_l = 0;
	guint8 ver_h = 0;
	guint8 boot_status = 0;

	/* sync bootloader flag from hardware state before doing anything else */
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK6,
					       FU_PIXART_TP_REG_SYS6_PLP239_BOOT_STATUS,
					       &boot_status,
					       error)) {
		g_prefix_error_literal(error, "failed to read boot status: ");
		return FALSE;
	}

	/* check if nav is ready to imply application mode */
	if ((boot_status & FU_PIXART_TP_BOOT_STATUS_PLP239_NAV_READY) == 0 || boot_status == 0xff) {
		g_debug("device is in bootloader mode (status: 0x%02x)", boot_status);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* read firmware version: 0x18 is high byte, 0x16 is low byte */
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK0,
					       FU_PIXART_TP_REG_SYS0_PLP239_VERSION_HIGH,
					       &ver_h,
					       error)) {
		g_prefix_error_literal(error, "failed to read version high: ");
		return FALSE;
	}
	if (!fu_pixart_tp_device_register_read(FU_PIXART_TP_DEVICE(self),
					       FU_PIXART_TP_SYSTEM_BANK_BANK0,
					       FU_PIXART_TP_REG_SYS0_PLP239_VERSION_LOW,
					       &ver_l,
					       error)) {
		g_prefix_error_literal(error, "failed to read version low: ");
		return FALSE;
	}

	/* success */
	fu_device_set_version_raw(device, (guint32)(ver_h << 8 | ver_l));
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_write_sections(FuPixartTpPlp239Device *self,
					  GPtrArray *sections,
					  FuProgress *progress,
					  GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, sections->len);

	for (guint i = 0; i < sections->len; i++) {
		FuPixartTpSection *section = g_ptr_array_index(sections, i);
		FuPixartTpUpdateType update_type = fu_pixart_tp_section_get_update_type(section);

		/* skip non-updatable sections */
		if (!fu_pixart_tp_section_has_flag(section, FU_PIXART_TP_FIRMWARE_FLAG_VALID) ||
		    fu_pixart_tp_section_has_flag(section,
						  FU_PIXART_TP_FIRMWARE_FLAG_IS_EXTERNAL)) {
			fu_progress_step_done(progress);
			continue;
		}

		if (update_type == FU_PIXART_TP_UPDATE_TYPE_TF_FORCE) {
			g_debug("skip TF_FORCE section %u for TP parent device", i);
			fu_progress_step_done(progress);
			continue;
		}

		if (fu_firmware_get_size(FU_FIRMWARE(section)) == 0) {
			fu_progress_step_done(progress);
			continue;
		}

		if (!fu_pixart_tp_plp239_device_write_section(self,
							      section,
							      fu_progress_get_child(progress),
							      error))
			return FALSE;

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);
	g_autoptr(GPtrArray) sections = NULL;
	guint64 total_update_bytes = 0;
	FuPixartTpSection *fw_section = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);

	/* sanity check */
	sections = fu_firmware_get_images(firmware);
	if (sections->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no sections to write");
		return FALSE;
	}

	/* calculate total bytes for valid internal TP sections
	 * NOTE:
	 * - TF_FORCE sections are skipped here; they are handled by the
	 * TF/haptic child-device using its own firmware image.
	 */
	for (guint i = 0; i < sections->len; i++) {
		FuPixartTpSection *section = g_ptr_array_index(sections, i);
		guint32 section_length = 0;
		FuPixartTpUpdateType update_type;

		if (!fu_pixart_tp_section_has_flag(section, FU_PIXART_TP_FIRMWARE_FLAG_VALID) ||
		    fu_pixart_tp_section_has_flag(section, FU_PIXART_TP_FIRMWARE_FLAG_IS_EXTERNAL))
			continue;

		update_type = fu_pixart_tp_section_get_update_type(section);
		if (update_type == FU_PIXART_TP_UPDATE_TYPE_TF_FORCE)
			continue;

		section_length = fu_firmware_get_size(FU_FIRMWARE(section));
		if (section_length > 0)
			total_update_bytes += (guint64)section_length;
	}

	/* sanity check */
	if (total_update_bytes == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no internal/valid TP sections to write");
		return FALSE;
	}
	g_debug("total TP update bytes=%" G_GUINT64_FORMAT, total_update_bytes);

	/* erase old firmware using 239 specific logic */
	fw_section = fu_pixart_tp_firmware_find_section_by_type(FU_PIXART_TP_FIRMWARE(firmware),
								FU_PIXART_TP_UPDATE_TYPE_FW_SECTION,
								error);
	if (fw_section != NULL) {
		guint32 start_address = fu_pixart_tp_section_get_target_flash_start(fw_section);
		if (!fu_pixart_tp_plp239_device_flash_erase_sector(
			self,
			start_address / FU_PIXART_TP_DEVICE_SECTOR_SIZE,
			error))
			return FALSE;
	}

	/* program all TP sections (TF_FORCE handled by child device) */
	if (!fu_pixart_tp_plp239_device_write_sections(self,
						       sections,
						       fu_progress_get_child(progress),
						       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify CRC (firmware + parameter) */
	if (!fu_pixart_tp_plp239_device_verify_crc(self,
						   FU_PIXART_TP_FIRMWARE(firmware),
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);

	/* nothing to do if already in application mode */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pixart_tp_plp239_device_reset(self, FU_PIXART_TP_RESET_MODE_APPLICATION, error))
		return FALSE;

	/* success */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);

	/* already in bootloader, nothing to do */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pixart_tp_plp239_device_reset(self, FU_PIXART_TP_RESET_MODE_BOOTLOADER, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_cleanup(FuDevice *device,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuPixartTpPlp239Device *self = FU_PIXART_TP_PLP239_DEVICE(device);

	/* ensure we are not stuck in bootloader */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pixart_tp_plp239_device_reset(self,
						      FU_PIXART_TP_RESET_MODE_APPLICATION,
						      error))
			return FALSE;
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_plp239_device_reload(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best-effort: do not fail the whole update just because reload failed */
	if (!fu_pixart_tp_plp239_device_setup(device, &error_local))
		g_debug("failed to refresh firmware version: %s", error_local->message);

	/* success */
	return TRUE;
}

static void
fu_pixart_tp_plp239_device_class_init(FuPixartTpPlp239DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_pixart_tp_plp239_device_setup;
	device_class->write_firmware = fu_pixart_tp_plp239_device_write_firmware;
	device_class->attach = fu_pixart_tp_plp239_device_attach;
	device_class->detach = fu_pixart_tp_plp239_device_detach;
	device_class->cleanup = fu_pixart_tp_plp239_device_cleanup;
	device_class->reload = fu_pixart_tp_plp239_device_reload;
}

static void
fu_pixart_tp_plp239_device_init(FuPixartTpPlp239Device *self)
{
}
