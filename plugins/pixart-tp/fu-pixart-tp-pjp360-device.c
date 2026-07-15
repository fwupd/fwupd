/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-pjp360-device.h"

struct _FuPixartTpPjp360Device {
	FuPixartTpDevice parent_instance;
};

G_DEFINE_TYPE(FuPixartTpPjp360Device, fu_pixart_tp_pjp360_device, FU_TYPE_PIXART_TP_DEVICE)

static gboolean
fu_pixart_tp_pjp360_device_reset(FuPixartTpPjp360Device *self,
				 FuPixartTpResetMode mode,
				 GError **error)
{
	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK1,
						FU_PIXART_TP_REG_SYS1_RESET_KEY1,
						FU_PIXART_TP_RESET_KEY1_SUSPEND,
						error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 30);

	if (!fu_pixart_tp_device_register_write(FU_PIXART_TP_DEVICE(self),
						FU_PIXART_TP_SYSTEM_BANK_BANK1,
						FU_PIXART_TP_REG_SYS1_RESET_KEY2,
						mode == FU_PIXART_TP_RESET_MODE_APPLICATION
						    ? FU_PIXART_TP_RESET_KEY2_REGULAR
						    : FU_PIXART_TP_RESET_KEY2_BOOTLOADER,
						error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), mode == FU_PIXART_TP_RESET_MODE_APPLICATION ? 500 : 10);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_pjp360_device_ensure_partid(FuPixartTpPjp360Device *self, GError **error)
{
	guint8 val = 0;
	guint16 version = 0;
	g_autoptr(GByteArray) buf = NULL;

	/* read tp part id */
	buf = fu_pixart_tp_device_register_read_array(FU_PIXART_TP_DEVICE(self),
						      FU_PIXART_TP_SYSTEM_BANK_BANK0,
						      FU_PIXART_TP_REG_SYS0_PART_ID,
						      2,
						      error);
	if (buf == NULL)
		return FALSE;

	if (!fu_memread_uint16_safe(buf->data, buf->len, 0x0, &version, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* define the extra instance IDs for quirks */
	fu_device_add_instance_u16(FU_DEVICE(self), "PARTID", version);
	fu_device_build_instance_id_full(FU_DEVICE(self),
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "PIXARTTP",
					 "PARTID",
					 NULL);

	/* verify part ID matches PJP360 */
	if (version != FU_PIXART_TP_PART_ID_PJP360) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unexpected part ID 0x%04x, expected 0x0360",
			    version);
		return FALSE;
	}

	/* read tp boot status */
	if (!fu_pixart_tp_device_register_user_read(FU_PIXART_TP_DEVICE(self),
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_BOOT_STAUS,
						    &val,
						    error))
		return FALSE;

	/* check if the IC is flashless */
	if (val == FU_PIXART_TP_BOOT_STATUS_FLASHLESS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flashless ICs do not support firmware updates");
		return FALSE;
	}

	/* reset to application mode if currently in ROM, then clear the bootloader flag */
	if (val == FU_PIXART_TP_BOOT_STATUS_ROM) {
		if (!fu_pixart_tp_pjp360_device_reset(self,
						      FU_PIXART_TP_RESET_MODE_APPLICATION,
						      error))
			return FALSE;
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_pjp360_device_get_rom_code_major(FuPixartTpPjp360Device *self,
					      guint8 *major,
					      GError **error)
{
	guint8 buf_padded[4] = {0};
	guint8 minor = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructPixartTpPjp360RomCodeVersion) st = NULL;

	/* read internal version to extract major for HID version address selection */
	buf = fu_pixart_tp_device_register_user_read_array(FU_PIXART_TP_DEVICE(self),
							   FU_PIXART_TP_USER_BANK_BANK0,
							   FU_PIXART_TP_REG_USER0_INTERNAL_VERSION,
							   2,
							   error);
	if (buf == NULL)
		return FALSE;

	/* pad to 4 bytes for b32 bitfield parse */
	if (!fu_memcpy_safe(buf_padded,
			    sizeof(buf_padded),
			    0x0,
			    buf->data,
			    buf->len,
			    0x0,
			    2,
			    error))
		return FALSE;
	st = fu_struct_pixart_tp_pjp360_rom_code_version_parse(buf_padded,
							       sizeof(buf_padded),
							       0x0,
							       error);
	if (st == NULL)
		return FALSE;

	*major = fu_struct_pixart_tp_pjp360_rom_code_version_get_major(st);
	minor = fu_struct_pixart_tp_pjp360_rom_code_version_get_minor(st);
	g_debug("rom code firmware version: %u.%u", *major, minor);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_pjp360_device_ensure_version(FuPixartTpPjp360Device *self, GError **error)
{
	guint16 version_raw = 0;
	guint8 major = 0;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_pixart_tp_pjp360_device_get_rom_code_major(self, &major, error))
		return FALSE;

	/*
	 * WORKAROUND: IC ECO swapped the version register address, so we use the ROM code major
	 * version to determine which register holds the firmware version
	 */
	buf = fu_pixart_tp_device_register_user_read_array(
	    FU_PIXART_TP_DEVICE(self),
	    FU_PIXART_TP_USER_BANK_BANK0,
	    (major == FU_PIXART_TP_VERSION_MAJOR_PJP360_ONE)
		? FU_PIXART_TP_REG_USER0_HID_VERSION_PJP360_MAJOR_ONE
		: FU_PIXART_TP_REG_USER0_HID_VERSION_PJP360_OTHER,
	    2,
	    error);
	if (buf == NULL)
		return FALSE;

	/* safely read 16-bit firmware version with bounds checking */
	if (!fu_memread_uint16_safe(buf->data, buf->len, 0x0, &version_raw, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* success */
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	return TRUE;
}

static gboolean
fu_pixart_tp_pjp360_device_setup(FuDevice *device, GError **error)
{
	FuPixartTpPjp360Device *self = FU_PIXART_TP_PJP360_DEVICE(device);

	if (!fu_pixart_tp_pjp360_device_ensure_partid(self, error))
		return FALSE;

	if (!fu_pixart_tp_pjp360_device_ensure_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_pjp360_device_reload(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best-effort: do not fail the whole update just because reload failed */
	if (!fu_pixart_tp_pjp360_device_setup(device, &error_local))
		g_debug("failed to refresh firmware version: %s", error_local->message);

	/* success */
	return TRUE;
}

static void
fu_pixart_tp_pjp360_device_class_init(FuPixartTpPjp360DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_pixart_tp_pjp360_device_setup;
	device_class->reload = fu_pixart_tp_pjp360_device_reload;
}

static void
fu_pixart_tp_pjp360_device_init(FuPixartTpPjp360Device *self)
{
}
