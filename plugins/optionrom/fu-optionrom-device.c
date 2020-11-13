/*
 * Copyright (C) 2015-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-rom.h"
#include "fu-optionrom-device.h"

struct _FuOptionromDevice {
	FuUdevDevice		 parent_instance;
};

G_DEFINE_TYPE (FuOptionromDevice, fu_optionrom_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_optionrom_device_probe (FuUdevDevice *device, GError **error)
{
	g_autofree gchar *fn = NULL;

	/* does the device even have ROM? */
	fn = g_build_filename (fu_udev_device_get_sysfs_path (device), "rom", NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Unable to read firmware from device");
		return FALSE;
	}

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (device, "pci", error))
		return FALSE;

	return TRUE;
}

static GBytes *
fu_optionrom_device_dump_firmware (FuDevice *device, GError **error)
{
	FuUdevDevice *udev_device = FU_UDEV_DEVICE (device);
	g_autoptr(GFile) file = NULL;
	g_autofree gchar *rom_fn = NULL;

	/* open the file */
	rom_fn = g_build_filename (fu_udev_device_get_sysfs_path (udev_device), "rom", NULL);
	if (rom_fn == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Unable to read firmware from device");
		return NULL;
	}
	file = g_file_new_for_path (rom_fn);
	return fu_rom_dump_firmware (file, NULL, error);
}

static FuFirmware *
fu_optionrom_device_read_firmware (FuDevice *device, GError **error)
{
	g_autofree gchar *guid = NULL;
	g_autoptr(FuRom) rom = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* open the file */
	blob = fu_optionrom_device_dump_firmware (device, error);
	if (blob == NULL)
		return NULL;
	rom = fu_rom_new ();
	if (!fu_rom_load_data (rom, blob, FU_ROM_LOAD_FLAG_BLANK_PPID, NULL, error))
		return NULL;

	/* update version */
	if (g_strcmp0 (fu_device_get_version (device),
		       fu_rom_get_version (rom)) != 0) {
		g_debug ("changing version of %s from %s to %s",
			 fu_device_get_id (device),
			 fu_device_get_version (device),
			 fu_rom_get_version (rom));
		fu_device_set_version (device, fu_rom_get_version (rom));
	}

	/* Also add the GUID from the firmware as the firmware may be more
	 * generic, which also allows us to match the GUID when doing 'verify'
	 * on a device with a different PID to the firmware */
	/* update guid */
	guid = g_strdup_printf ("PCI\\VEN_%04X&DEV_%04X",
				fu_rom_get_vendor (rom),
				fu_rom_get_model (rom));
	fu_device_add_guid (device, guid);

	/* get new data */
	fw = fu_rom_get_data (rom);
	return fu_firmware_new_from_bytes (fw);
}

static void
fu_optionrom_device_init (FuOptionromDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_logical_id (FU_DEVICE (self), "rom");
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
				  FU_UDEV_DEVICE_FLAG_OPEN_READ |
				  FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static void
fu_optionrom_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_optionrom_device_parent_class)->finalize (object);
}

static void
fu_optionrom_device_class_init (FuOptionromDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_optionrom_device_finalize;
	klass_device->read_firmware = fu_optionrom_device_read_firmware;
	klass_device->dump_firmware = fu_optionrom_device_dump_firmware;
	klass_udev_device->probe = fu_optionrom_device_probe;
}
