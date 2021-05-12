/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-context.h"
#include "fu-parade-lspcon-device.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#define I2C_PATH_REGEX "/i2c-([0-9]+)/"
#define HID_LENGTH 8

struct _FuParadeLspconDevice {
	FuUdevDevice	  	  parent_instance;
	FuFlashromOpener 	 *opener;
	gint			  bus_number;
	guint8			  active_partition;
};

G_DEFINE_TYPE (FuParadeLspconDevice, fu_parade_lspcon_device,
	       FU_TYPE_UDEV_DEVICE)

static const struct fu_flashrom_opener_layout_region FLASH_REGIONS[] = {
	{ .offset = 0x00002, .size = 2, .name = "FLAG" },
	{ .offset = 0x10000, .size = 0x10000, .name = "PAR1" },
	{ .offset = 0x20000, .size = 0x10000, .name = "PAR2" },
	{ .offset = 0x15000, .size = 3, .name = "VER1" },
	{ .offset = 0x25000, .size = 3, .name = "VER2" },
	{ .offset = 0x35000, .size = 3, .name = "VERBOOT" },
};

static void
fu_parade_lspcon_device_init (FuParadeLspconDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_vendor_id (FU_DEVICE(self), "PCI:0x1AF8");
	fu_device_set_vendor (FU_DEVICE (self), "Parade Technologies");
	fu_device_add_protocol (FU_DEVICE (self), "com.paradetech.ps175");

	self->opener = fu_flashrom_opener_new();
	fu_flashrom_opener_set_programmer (self->opener, "lspcon_i2c_spi");
	fu_flashrom_opener_set_layout (self->opener, FLASH_REGIONS,
				       sizeof (FLASH_REGIONS) / sizeof (*FLASH_REGIONS));
}

static gboolean
lspcon_device_set_instance_ids (FuParadeLspconDevice *self, GError **error)
{
	g_autofree gchar *vid;
	g_autofree gchar *pid;
	g_autofree gchar *instance_id;
	g_autofree gchar *physical_id;
	const gchar *hw_id = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (self),
							    "name", error);
	if (hw_id == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HID not found");
		return FALSE;
	}
	vid = g_strndup (hw_id, HID_LENGTH / 2);
	pid = g_strndup (&hw_id[HID_LENGTH / 2], HID_LENGTH / 2);

	instance_id = g_strdup_printf ("PARADE-LSPCON\\NAME_%s", hw_id);
	fu_device_add_instance_id (FU_DEVICE (self), instance_id);
	g_free (instance_id);
	instance_id = g_strdup_printf ("FLASHROM-LSPCON-I2C-SPI\\VEN_%s&DEV_%s", vid, pid);
	fu_device_add_instance_id (FU_DEVICE (self), instance_id);

	physical_id = g_strdup_printf ("DEVNAME=%s",
				       fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (self)));
	fu_device_set_physical_id (FU_DEVICE (self), physical_id);
	return TRUE;
}

static gboolean
fu_parade_lspcon_device_probe (FuDevice *device, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE (device);
	FuDeviceClass *klass =
		FU_DEVICE_CLASS (fu_parade_lspcon_device_parent_class);
	g_autoptr(GRegex) regex = NULL;
	g_autoptr(GMatchInfo) info = NULL;
	const gchar *path = NULL;
	g_autofree gchar *programmer_args = NULL;

	if (!lspcon_device_set_instance_ids (FU_PARADE_LSPCON_DEVICE (self),
					     error))
		return FALSE;
	if (g_strcmp0 (fu_device_get_name (device), "PS175") != 0) {
		g_set_error_literal (error, FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "only PS175 devices are supported");
		return FALSE;
	}

	/* get bus number out of sysfs path */
	path = fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device));
	regex = g_regex_new (I2C_PATH_REGEX, 0, 0, error);
	if (!regex || !g_regex_match_full (regex, path, -1, 0, 0, &info,
					   error))
		return FALSE;
	self->bus_number = g_ascii_strtoll (g_match_info_fetch (info, 1),
					    NULL, 10);

	/* set up opener to use detected bus */
	programmer_args = g_strdup_printf ("bus=%d", self->bus_number);
	fu_flashrom_opener_set_programmer_args (self->opener, programmer_args);

	return klass->probe (device, error);
}

static gboolean
fu_parade_lspcon_device_open (FuDevice *device, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE (device);
	FuDeviceClass *klass =
		FU_DEVICE_CLASS (fu_parade_lspcon_device_parent_class);
	g_autofree gchar *bus_path = NULL;
	gint bus_fd;

	/* open the bus, not the device represented by self */
	bus_path = g_strdup_printf ("/dev/i2c-%d", self->bus_number);
	g_debug ("communicating with device on %s", bus_path);
	if ((bus_fd = g_open (bus_path, O_RDWR, 0)) == -1) {
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
			     "failed to open %s read-write", bus_path);
		return FALSE;
	}
	fu_udev_device_set_fd (FU_UDEV_DEVICE (self), bus_fd);
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self), FU_UDEV_DEVICE_FLAG_NONE);

	return klass->open (device, error);
}

static gboolean
probe_active_flash_partition (FuParadeLspconDevice *self,
			      guint8 *partition,
			      GError **error)
{
	guint8 data;

	/* read register 0x0e on page 5, which is set to the currently-running
	 * flash partition number */
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   I2C_SLAVE, (guint8 *) (0x9a >> 1), NULL, error)) {
		g_prefix_error (error, "failed to set I2C slave address: ");
		return FALSE;
	}
	if (!fu_udev_device_pwrite (FU_UDEV_DEVICE (self), 0, 0x0e, error)) {
		g_prefix_error (error, "failed to write register address: ");
		return FALSE;
	}
	if (!fu_udev_device_pread (FU_UDEV_DEVICE (self), 0, &data, error)) {
		g_prefix_error (error, "failed to read register value: ");
		return FALSE;
	}
	*partition = data;
	return TRUE;
}

static gboolean
fu_parade_lspcon_device_set_version (FuDevice *device, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE (device);
	g_autoptr(FuFlashromContext) context = NULL;
	const gchar *active_partition_name;
	g_autoptr(GBytes) contents = NULL;
	const guint8 *contents_buf;
	gsize contents_size;
	guint32 version_addr;
	g_autofree gchar *version = NULL;

	if (!fu_flashrom_context_open (self->opener, &context, error))
		return FALSE;

	/* get the active partition */
	if (!probe_active_flash_partition (self, &self->active_partition, error))
		return FALSE;
	g_debug ("device reports running from partition %d", self->active_partition);

	switch (self->active_partition) {
		case 1:
			active_partition_name = "VER1";
			break;
		case 2:
			active_partition_name = "VER2";
			break;
		case 3:
			active_partition_name = "VERBOOT";
			break;
		default:
			g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_BROKEN_SYSTEM,
				     "Unexpected active flash partition: %d",
				     self->active_partition);
			return FALSE;
	}

	/* read version bytes for the active partition from device flash */
	if (!fu_flashrom_context_set_included_regions (context, error,
						       active_partition_name,
						       NULL))
		return FALSE;
	if (!fu_flashrom_context_read_image (context, &contents, error))
		return FALSE;
	contents_buf = g_bytes_get_data (contents, &contents_size);

	/* extract the active partition's version number */
	version_addr = FLASH_REGIONS[self->active_partition + 2].offset;
	g_return_val_if_fail (version_addr < (contents_size - 2), FALSE);
	version = g_strdup_printf ("%d.%d", contents_buf[version_addr],
				   contents_buf[version_addr + 2]);
	fu_device_set_version (device, version);

	return TRUE;
}

static gboolean
fu_parade_lspcon_device_write_firmware (FuDevice *device,
						  FuFirmware *firmware,
						  FwupdInstallFlags flags,
						  GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE (device);
	g_autoptr(FuFlashromContext) context = NULL;
	gsize flash_size;
	guint8 *flash_contents_buf;
	g_autoptr(GBytes) flash_contents = NULL;
	/* if the boot partition is active we could flash either, but prefer
	 * the first */
	const guint8 target_partition = self->active_partition == 1 ? 2 : 1;
	const gsize region_size = FLASH_REGIONS[target_partition].size;
	gsize fw_size;
	const guint8 *fw_buf;
	g_autoptr(GBytes) blob_fw = fu_firmware_get_bytes (firmware, error);
	if (blob_fw == NULL)
		return FALSE;

	if (!fu_flashrom_context_open (self->opener, &context, error))
		return FALSE;
	flash_size = fu_flashrom_context_get_flash_size (context);
	flash_contents_buf = g_malloc0 (flash_size);
	flash_contents = g_bytes_new_take (flash_contents_buf, flash_size);

	/* copy firmware blob to flash image at position of target partition */
	fw_buf = g_bytes_get_data (blob_fw, &fw_size);
	if (fw_size != region_size) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid image size 0x%x, expected 0x%x",
			     (guint) fw_size, (guint) flash_size);
		return FALSE;
	}
	if (!fu_memcpy_safe (flash_contents_buf, flash_size,
			     FLASH_REGIONS[target_partition].offset,
			     fw_buf, fw_size, 0,
			     region_size, error))
		return FALSE;


	/* write target_partition only, flashing the new version */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	fu_device_set_progress (device, 0); /* urgh */
	if (!fu_flashrom_context_set_included_regions (context, error,
						       FLASH_REGIONS[target_partition].name,
						       NULL))
		return FALSE;
	if (!fu_flashrom_context_write_image (context, flash_contents, TRUE, error))
		return FALSE;


	/* write flag area to point to the newly-flashed version */
	if (!fu_flashrom_context_set_included_regions (context, error, "FLAG", NULL))
		return FALSE;

	/* Flag area is header bytes (0x55, 0xAA) followed by the bank ID to
	 * boot from (1 or 2) and the two's complement inverse of that bank ID
	 * (0 or 0xFF). We only write bytes 2 and 3, assuming the header is
	 * already valid. */
	flash_contents_buf[2] = (gint8) target_partition;
	flash_contents_buf[3] = (gint8) -target_partition + 1;

	if (!fu_flashrom_context_write_image (context, flash_contents, TRUE, error))
		return FALSE;

	return TRUE;
}

static void
fu_parade_lspcon_device_class_init (FuParadeLspconDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_parade_lspcon_device_probe;
	klass_device->open = fu_parade_lspcon_device_open;
	klass_device->setup = fu_parade_lspcon_device_set_version;
	klass_device->write_firmware = fu_parade_lspcon_device_write_firmware;
	klass_device->reload = fu_parade_lspcon_device_set_version;
}
