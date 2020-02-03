/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-vli-pd-common.h"
#include "fu-vli-pd-firmware.h"

#include "fu-vli-usbhub-common.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-pd-common.h"
#include "fu-vli-usbhub-pd-device.h"

struct _FuVliUsbhubPdDevice
{
	FuDevice		 parent_instance;
	FuVliPdHdr		 hdr;
	FuVliDeviceKind		 device_kind;
};

G_DEFINE_TYPE (FuVliUsbhubPdDevice, fu_vli_usbhub_pd_device, FU_TYPE_DEVICE)

static void
fu_vli_usbhub_pd_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE (device);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (self->device_kind));
	fu_common_string_append_kx (str, idt, "FwOffset",
				    fu_vli_common_device_kind_get_offset (self->device_kind));
	fu_common_string_append_kx (str, idt, "FwSize",
				    fu_vli_common_device_kind_get_size (self->device_kind));
}

static gboolean
fu_vli_usbhub_pd_device_probe (FuDevice *device, GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE (device);

	guint32 fwver;
	g_autofree gchar *fwver_str = NULL;
	g_autofree gchar *instance_id1 = NULL;

	/* get version */
	fwver = GUINT32_FROM_BE (self->hdr.fwver);
	self->device_kind = fu_vli_pd_common_guess_device_kind (fwver);
	if (self->device_kind == FU_VLI_DEVICE_KIND_UNKNOWN) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "PD version invalid [0x%x]", fwver);
		return FALSE;
	}
	fu_device_set_name (device, fu_vli_common_device_kind_to_string (self->device_kind));

	/* use header to populate device info */
	fwver_str = fu_common_version_from_uint32 (fwver, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version (device, fwver_str, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version_raw (device, fwver);
	instance_id1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&DEV_%s",
					GUINT16_FROM_LE (self->hdr.vid),
					GUINT16_FROM_LE (self->hdr.pid),
					fu_vli_common_device_kind_to_string (self->device_kind));
	fu_device_add_instance_id (device, instance_id1);

	/* these have a backup section */
	if (fu_vli_common_device_kind_get_offset (self->device_kind) == VLI_USBHUB_FLASHMAP_ADDR_PD)
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_usbhub_pd_device_prepare_firmware (FuDevice *device,
				       GBytes *fw,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE (device);
	FuVliDeviceKind device_kind;
	g_autoptr(FuFirmware) firmware = fu_vli_pd_firmware_new ();

	/* add the two offset locations the header can be found */
	fu_vli_pd_firmware_add_offset (FU_VLI_PD_FIRMWARE (firmware), VLI_USBHUB_PD_FLASHMAP_ADDR_LEGACY);
	fu_vli_pd_firmware_add_offset (FU_VLI_PD_FIRMWARE (firmware), VLI_USBHUB_PD_FLASHMAP_ADDR);

	/* check size */
	if (g_bytes_get_size (fw) < fu_device_get_firmware_size_min (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too small, got 0x%x, expected >= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_min (device));
		return NULL;
	}
	if (g_bytes_get_size (fw) > fu_device_get_firmware_size_max (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too large, got 0x%x, expected <= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_max (device));
		return NULL;
	}

	/* check is compatible with firmware */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	device_kind = fu_vli_pd_firmware_get_kind (FU_VLI_PD_FIRMWARE (firmware));
	if (self->device_kind != device_kind) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware incompatible, got %s, expected %s",
			     fu_vli_common_device_kind_to_string (device_kind),
			     fu_vli_common_device_kind_to_string (self->device_kind));
		return NULL;
	}

	/* we could check this against flags */
	g_debug ("parsed version: %s", fu_firmware_get_version (firmware));
	return g_steal_pointer (&firmware);
}

static FuFirmware *
fu_vli_usbhub_pd_device_read_firmware (FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE (fu_device_get_parent (device));
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return NULL;

	/* read */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_VERIFY);
	fw = fu_vli_device_spi_read (FU_VLI_DEVICE (parent),
				     fu_vli_common_device_kind_get_offset (self->device_kind),
				     fu_device_get_firmware_size_max (device),
				     error);
	if (fw == NULL)
		return NULL;
	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_vli_usbhub_pd_device_write_firmware (FuDevice *device,
					FuFirmware *firmware,
					FwupdInstallFlags flags,
					GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE (device);
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE (fu_device_get_parent (device));
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* simple image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;

	/* erase */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	buf = g_bytes_get_data (fw, &bufsz);
	if (!fu_vli_device_spi_erase (FU_VLI_DEVICE (parent),
				      fu_vli_common_device_kind_get_offset (self->device_kind),
				      bufsz, error))
		return FALSE;

	/* write */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_vli_device_spi_write (FU_VLI_DEVICE (parent),
				      fu_vli_common_device_kind_get_offset (self->device_kind),
				      buf, bufsz, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_vli_usbhub_pd_device_init (FuVliUsbhubPdDevice *self)
{
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_set_protocol (FU_DEVICE (self), "com.vli.usbhub");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_install_duration (FU_DEVICE (self), 15); /* seconds */
	fu_device_set_logical_id (FU_DEVICE (self), "PD");
	fu_device_set_summary (FU_DEVICE (self), "USB-C Power Delivery Device");
}

static void
fu_vli_usbhub_pd_device_class_init (FuVliUsbhubPdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_usbhub_pd_device_to_string;
	klass_device->probe = fu_vli_usbhub_pd_device_probe;
	klass_device->read_firmware = fu_vli_usbhub_pd_device_read_firmware;
	klass_device->write_firmware = fu_vli_usbhub_pd_device_write_firmware;
	klass_device->prepare_firmware = fu_vli_usbhub_pd_device_prepare_firmware;
}

FuDevice *
fu_vli_usbhub_pd_device_new (FuVliPdHdr *hdr)
{
	FuVliUsbhubPdDevice *self = g_object_new (FU_TYPE_VLI_USBHUB_PD_DEVICE, NULL);
	memcpy (&self->hdr, hdr, sizeof(self->hdr));
	return FU_DEVICE (self);
}
