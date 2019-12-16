/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-firmware.h"

#include "fu-vli-pd-device.h"

struct _FuVliPdDevice
{
	FuUsbDevice		 parent_instance;
};

G_DEFINE_TYPE (FuVliPdDevice, fu_vli_pd_device, FU_TYPE_VLI_DEVICE)

static gboolean
fu_vli_pd_device_reg_write (FuVliPdDevice *self, guint16 addr, guint8 value, GError **error)
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
fu_vli_pd_device_reg_read (FuVliPdDevice *self, guint16 addr, guint8 *value, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xe0,
					    ((addr & 0xff) << 8) | 0x01,
					    addr >> 8,
					    value, 0x1, NULL,
					    1000, NULL, error)) {
		g_prefix_error (error, "failed to read register 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_setup (FuVliDevice *vli_device, GError **error)
{
	//FuVliPdDevice *self = FU_VLI_PD_DEVICE (vli_device);

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

static gboolean
fu_vli_pd_device_write_firmware (FuDevice *device,
				 FuFirmware *firmware,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
	guint8 tmp = 0;
	gboolean is_vl103 = FALSE;

	/* write GPIO 4&5, 7 then 3 */
	if (!fu_vli_pd_device_reg_write (self, 0x0015, 0x7F, error))
		return FALSE;
	if (!fu_vli_pd_device_reg_write (self, 0x0019, 0x00, error))
		return FALSE;
	if (!fu_vli_pd_device_reg_write (self, 0x001C, 0x02, error))
		return FALSE;

	/* is VL103, i.e. new reset VDR_0xC0 */
	if (!fu_vli_pd_device_reg_read (self, 0x0018, &tmp, error))
		return FALSE;
	if (tmp == 0x80) // 0x90 is VL104
		is_vl103 = 1;

	/* reset from SPI_Code into ROM_Code */
	for (guint i = 0; i < 6; i++) {
		g_autoptr(GError) error_local = NULL;
		tmp = 0xff;

		/* try to read status */
		if (!fu_vli_pd_device_reg_read (self, 0x00F7, &tmp, &error_local)) {
			if (i == 5) {
				g_propagate_error (error, g_steal_pointer (&error_local));
				return FALSE;
			}
			g_debug ("ignoring: %s", error_local->message);
		} else {
			if ((tmp & 0x80) == 0x00) {
				break;
			} else if (is_vl103) {
				if (!fu_vli_device_vdr_reg_read (FU_VLI_DEVICE (self),
								 0xC0, 0x0, NULL, 0x0, error))
					return FALSE;
			} else {
				// Patch APP5 FW-Bug(2AF2->2AE2)
				if (!fu_vli_pd_device_reg_write (self, 0x2AE2, 0x1E, error))
					return FALSE;
				if (!fu_vli_pd_device_reg_write (self, 0x2AE3, 0xC3, error))
					return FALSE;
				if (!fu_vli_pd_device_reg_write (self, 0x2AE4, 0x5A, error))
					return FALSE;
				if (!fu_vli_pd_device_reg_write (self, 0x2AE5, 0x87, error))
					return FALSE;
				if (!fu_vli_device_vdr_reg_read (FU_VLI_DEVICE (self),
								 0xA0, 0x0, NULL, 0x0, error))
					return FALSE;
				if (!fu_vli_device_vdr_reg_read (FU_VLI_DEVICE (self),
								 0xB0, 0x0, NULL, 0x0, error))
					return FALSE;
			}
		}
		g_usleep (1900 * 1000);
	}

	/* write GPIO 4&5, 7 then 3 */
	if (!fu_vli_pd_device_reg_write (self, 0x0015, 0x7F, error))
		return FALSE;
	if (!fu_vli_pd_device_reg_write (self, 0x0019, 0x00, error))
		return FALSE;
	if (!fu_vli_pd_device_reg_write (self, 0x001C, 0x02, error))
		return FALSE;

	/* TODO: implement FW_DL_Exe here */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "update protocol not supported");
	return FALSE;
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
	klass_vli_device->setup = fu_vli_pd_device_setup;
}
