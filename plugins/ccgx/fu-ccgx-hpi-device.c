/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-hpi-device.h"
#include "fu-ccgx-cyacd-firmware.h"

struct _FuCcgxHpiDevice
{
	FuUsbDevice		 parent_instance;
	guint16			 silicon_id;
	guint16			 fw_app_type;
};

G_DEFINE_TYPE (FuCcgxHpiDevice, fu_ccgx_hpi_device, FU_TYPE_USB_DEVICE)

static void
fu_ccgx_hpi_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	fu_common_string_append_kx (str, idt, "SiliconId", self->silicon_id);
	fu_common_string_append_kx (str, idt, "FwAppType", self->fw_app_type);
}

static gboolean
fu_ccgx_hpi_device_attach (FuDevice *device, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported");
	return FALSE;
}

static FuFirmware *
fu_ccgx_hpi_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autoptr(FuFirmware) firmware = fu_ccgx_cyacd_firmware_new ();
	g_autoptr(GPtrArray) images = NULL;

	/* parse all images */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check the silicon ID of all images */
	images = fu_firmware_get_images (firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (images, i);
		if (fu_firmware_image_get_addr (img) != self->silicon_id) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "silicon id mismatch on image %u, "
				     "expected 0x%x, got 0x%x",
				     i, self->silicon_id,
				     (guint) fu_firmware_image_get_addr (img));
			return NULL;
		}
	}

	return g_steal_pointer (&firmware);
}

static gboolean
fu_ccgx_hpi_write_firmware (FuDevice *device,
			    FuFirmware *firmware,
			    FwupdInstallFlags flags,
			    GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported");
	return FALSE;
}

static gboolean
fu_ccgx_hpi_device_setup (FuDevice *device, GError **error)
{
	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_set_quirk_kv (FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	if (g_strcmp0 (key, "SiliconId") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			self->silicon_id = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid SiliconId");
		return FALSE;
	}
	if (g_strcmp0 (key, "FwAppType") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			self->fw_app_type = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid FwAppType");
		return FALSE;
	}
	return FALSE;
}

static gboolean
fu_ccgx_hpi_device_open (FuUsbDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	if (!g_usb_device_claim_interface (fu_usb_device_get_dev (device), 0x0,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot claim interface: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_close (FuUsbDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	if (!g_usb_device_release_interface (fu_usb_device_get_dev (device), 0x0,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot release interface: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static void
fu_ccgx_hpi_device_init (FuCcgxHpiDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.cypress.ccgx");
	fu_device_set_install_duration (FU_DEVICE (self), 60);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
}

static void
fu_ccgx_hpi_device_class_init (FuCcgxHpiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->to_string = fu_ccgx_hpi_device_to_string;
	klass_device->write_firmware = fu_ccgx_hpi_write_firmware;
	klass_device->prepare_firmware = fu_ccgx_hpi_device_prepare_firmware;
	klass_device->attach = fu_ccgx_hpi_device_attach;
	klass_device->setup = fu_ccgx_hpi_device_setup;
	klass_device->set_quirk_kv = fu_ccgx_hpi_device_set_quirk_kv;
	klass_usb_device->open = fu_ccgx_hpi_device_open;
	klass_usb_device->close = fu_ccgx_hpi_device_close;
}
