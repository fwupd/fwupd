/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-optionrom-device.h"

struct _FuOptionromDevice {
	FuPciDevice parent_instance;
};

G_DEFINE_TYPE(FuOptionromDevice, fu_optionrom_device, FU_TYPE_PCI_DEVICE)

static gboolean
fu_optionrom_device_probe(FuDevice *device, GError **error)
{
	g_autofree gchar *fn = NULL;

	/* does the device even have ROM? */
	fn = g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)), "rom", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Unable to read firmware from device");
		return FALSE;
	}
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(device), fn);
	return TRUE;
}

static GBytes *
fu_optionrom_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GBytes) fw = NULL;

	/* FuUdevDevice->dump_firmware */
	fw = FU_DEVICE_CLASS(fu_optionrom_device_parent_class)
		 ->dump_firmware(device, progress, error);
	if (fw == NULL)
		return NULL;
	if (g_bytes_get_size(fw) < 512) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware too small: %u bytes",
			    (guint)g_bytes_get_size(fw));
		return NULL;
	}
	return g_steal_pointer(&fw);
}

static void
fu_optionrom_device_init(FuOptionromDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_logical_id(FU_DEVICE(self), "rom");
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
}

static void
fu_optionrom_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_optionrom_device_parent_class)->finalize(object);
}

static void
fu_optionrom_device_class_init(FuOptionromDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_optionrom_device_finalize;
	device_class->dump_firmware = fu_optionrom_device_dump_firmware;
	device_class->probe = fu_optionrom_device_probe;
}
