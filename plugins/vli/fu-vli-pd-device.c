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
	/* TODO: implement ExecuteInitialize here */

	/* not done yet */
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
