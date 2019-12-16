/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-firmware.h"

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-firmware.h"

struct _FuVliPdDevice
{
	FuUsbDevice		 parent_instance;
};

G_DEFINE_TYPE (FuVliPdDevice, fu_vli_pd_device, FU_TYPE_VLI_DEVICE)

static gboolean
fu_vli_pd_device_vdr_reg_write (FuVliPdDevice *self, guint16 addr, guint8 value, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xe0,
					    ((addr & 0xff) << 8) | 0x02,
					    addr >> 8,
					    &value, sizeof(value), NULL,
					    1000, NULL, error)) {
		g_prefix_error (error,
				"failed to write register 0x%x value 0x%x: ",
				addr, value);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_vdr_reg_read (FuVliPdDevice *self, guint16 addr, guint8 *value, GError **error)
{
	return fu_vli_device_raw_read (FU_VLI_DEVICE (self),
				       0xe0,
				       ((addr & 0xff) << 8) | 0x01,
				       addr >> 8, value, 0x1, error);
}

static gboolean
fu_vli_pd_device_setup (FuVliDevice *vli_device, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (vli_device);
	FwupdVersionFormat verfmt = fu_device_get_version_format (FU_DEVICE (self));
	guint32 version_raw;
	guint8 verbuf[4] = { 0x0 };
	g_autofree gchar *version_str = NULL;

	/* get version */
	if (!fu_vli_device_raw_read (FU_VLI_DEVICE (self),
					 0xe0, 0x01, 0x00,
					 verbuf, sizeof(verbuf), error))
		return FALSE;
	version_raw = fu_common_read_uint32 (verbuf, G_BIG_ENDIAN);
	fu_device_set_version_raw (FU_DEVICE (self), version_raw);
	version_str = g_strdup_printf ("%02X.%02X.%02X.%02X",
				       verbuf[0], verbuf[1], verbuf[2], verbuf[3]);
	fu_device_set_version (FU_DEVICE (self), version_str, verfmt);

	/* TODO: detect any I²C child, e.g. parade device */

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_pd_device_read_firmware (FuDevice *device, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	fw = fu_vli_device_spi_read_all (FU_VLI_DEVICE (self), 0x0,
					 fu_device_get_firmware_size_max (device),
					 error);
	if (fw == NULL)
		return NULL;
	return fu_firmware_new_from_bytes (fw);
}

static FuFirmware *
fu_vli_pd_device_prepare_firmware (FuDevice *device,
				   GBytes *fw,
				   FwupdInstallFlags flags,
				   GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_vli_pd_firmware_new ();

	/* check size */
	if (g_bytes_get_size (fw) > fu_device_get_firmware_size_max (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too large, got 0x%x, expected <= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_max (device));
		return NULL;
	}

	/* check CRC is correct */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_vli_pd_device_reset_to_romcode (FuVliPdDevice *self, GError **error)
{
	guint8 tmp = 0;
	gboolean is_vl103 = FALSE;

	/* is VL103, i.e. new reset VDR_0xC0 */
	if (!fu_vli_pd_device_vdr_reg_read (self, 0x0018, &tmp, error))
		return FALSE;
	if (tmp == 0x80) // 0x90 is VL104
		is_vl103 = 1;

	/* reset from SPI_Code into ROM_Code */
	for (guint i = 0; i < 6; i++) {
		g_autoptr(GError) error_local = NULL;

		/* try to read status */
		if (!fu_vli_pd_device_vdr_reg_read (self, 0x00F7, &tmp, &error_local)) {
			if (i == 5) {
				g_propagate_error (error, g_steal_pointer (&error_local));
				return FALSE;
			}
			g_debug ("ignoring: %s", error_local->message);
		} else {
			if ((tmp & 0x80) == 0x00) {
				break;
			} else if (is_vl103) {
				if (!fu_vli_device_raw_read (FU_VLI_DEVICE (self),
								 0xC0, 0x0, 0x0,
								 NULL, 0x0, error))
					return FALSE;
			} else {
				/* patch APP5 FW bug (2AF2 -> 2AE2) */
				if (!fu_vli_pd_device_vdr_reg_write (self, 0x2AE2, 0x1E, error))
					return FALSE;
				if (!fu_vli_pd_device_vdr_reg_write (self, 0x2AE3, 0xC3, error))
					return FALSE;
				if (!fu_vli_pd_device_vdr_reg_write (self, 0x2AE4, 0x5A, error))
					return FALSE;
				if (!fu_vli_pd_device_vdr_reg_write (self, 0x2AE5, 0x87, error))
					return FALSE;
				if (!fu_vli_device_raw_read (FU_VLI_DEVICE (self),
							     0xA0, 0x0, 0x0,
							     NULL, 0x0, error))
					return FALSE;
				if (!fu_vli_device_raw_read (FU_VLI_DEVICE (self),
							     0xB0, 0x0, 0x0,
							     NULL, 0x0, error))
					return FALSE;
			}
		}
		g_usleep (1900 * 1000);
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_write_gpios (FuVliPdDevice *self, GError **error)
{
	/* write GPIO 4&5, 7 then 3 */
	if (!fu_vli_pd_device_vdr_reg_write (self, 0x0015, 0x7F, error))
		return FALSE;
	if (!fu_vli_pd_device_vdr_reg_write (self, 0x0019, 0x00, error))
		return FALSE;
	if (!fu_vli_pd_device_vdr_reg_write (self, 0x001C, 0x02, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_device_write_firmware (FuDevice *device,
				 FuFirmware *firmware,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
	gsize bufsz = 0;
	guint8 tmp = 0;
	const guint8 *buf = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* binary blob */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* write GPIOs */
	if (!fu_vli_pd_device_write_gpios (self, error))
		return FALSE;

	/* reset from SPI_Code into ROM_Code */
	if (!fu_vli_pd_device_reset_to_romcode (self, error))
		return FALSE;

	/* write GPIOs in new mode */
	if (!fu_vli_pd_device_write_gpios (self, error))
		return FALSE;

	/* disable write protect in GPIO_3 */
	if (!fu_vli_pd_device_vdr_reg_read (self, 0x0003, &tmp, error))
		return FALSE;
	if (!fu_vli_pd_device_vdr_reg_write (self, 0x0003, tmp | 0x44, error))
		return FALSE;

	/* erase */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_vli_device_spi_erase_all (FU_VLI_DEVICE (self), error))
		return FALSE;

	/* write in chunks */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	buf = g_bytes_get_data (fw, &bufsz);
	if (!fu_vli_device_spi_write (FU_VLI_DEVICE (self), 0x0000000000000000000000000000000000000000000000000,
				      buf, bufsz, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_vli_pd_device_init (FuVliPdDevice *self)
{
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_set_protocol (FU_DEVICE (self), "com.vli.pd");
	fu_device_set_summary (FU_DEVICE (self), "USB PD");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_vli_pd_device_class_init (FuVliPdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuVliDeviceClass *klass_vli_device = FU_VLI_DEVICE_CLASS (klass);
	klass_device->read_firmware = fu_vli_pd_device_read_firmware;
	klass_device->write_firmware = fu_vli_pd_device_write_firmware;
	klass_device->prepare_firmware = fu_vli_pd_device_prepare_firmware;
	klass_vli_device->setup = fu_vli_pd_device_setup;
}
