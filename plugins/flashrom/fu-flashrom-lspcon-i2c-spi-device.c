/*
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-device.h"
#include "fu-flashrom-lspcon-i2c-spi-device.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <libflashrom.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#define I2C_PATH_REGEX "/i2c-([0-9]+)/"
#define HID_LENGTH 8

struct _FuFlashromLspconI2cSpiDevice {
	FuFlashromDevice	  parent_instance;
	gint			  bus_number;
	guint8			  active_partition;
};

G_DEFINE_TYPE (FuFlashromLspconI2cSpiDevice, fu_flashrom_lspcon_i2c_spi_device,
	       FU_TYPE_FLASHROM_DEVICE)

static const struct fu_flashrom_opener_layout_region FLASH_REGIONS[] = {
	{ .offset = 0x00002, .size = 2, .name = "FLAG" },
	{ .offset = 0x10000, .size = 0x10000, .name = "PAR1" },
	{ .offset = 0x20000, .size = 0x10000, .name = "PAR2" },
	{ .offset = 0x15000, .size = 3, .name = "VER1" },
	{ .offset = 0x25000, .size = 3, .name = "VER2" },
	{ .offset = 0x35000, .size = 3, .name = "VERBOOT" },
};

static void
fu_flashrom_lspcon_i2c_spi_device_init (FuFlashromLspconI2cSpiDevice *self)
{
	FuFlashromOpener *opener = fu_flashrom_device_get_opener (FU_FLASHROM_DEVICE (self));

	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);

	fu_flashrom_opener_set_layout (opener, FLASH_REGIONS,
				       sizeof (FLASH_REGIONS) / sizeof (*FLASH_REGIONS));
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_probe (FuDevice *device, GError **error)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	FuFlashromDevice *flashrom_device = FU_FLASHROM_DEVICE (device);
	FuDeviceClass *klass =
		FU_DEVICE_CLASS (fu_flashrom_lspcon_i2c_spi_device_parent_class);
	g_autoptr(GRegex) regex = NULL;
	g_autoptr(GMatchInfo) info = NULL;
	const gchar *path = NULL;

	/* FuFlashromDevice->probe */
	if (!klass->probe (device, error))
		return FALSE;

	if (g_strcmp0 (fu_flashrom_device_get_programmer_name (flashrom_device),
		       "lspcon_i2c_spi") != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid programmer");
		return FALSE;
	}

	/* get bus number out of sysfs path */
	path = fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device));
	regex = g_regex_new (I2C_PATH_REGEX, 0, 0, error);
	if (regex && g_regex_match_full (regex, path, -1, 0, 0, &info, error)) {
		self->bus_number = g_ascii_strtoll ( g_match_info_fetch (info, 1),
					       NULL, 10);
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_open (FuDevice *device,
					GError **error)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	FuFlashromDevice *flashrom_device = FU_FLASHROM_DEVICE (device);
	FuDeviceClass *klass =
		FU_DEVICE_CLASS (fu_flashrom_lspcon_i2c_spi_device_parent_class);
	g_autofree gchar *temp = NULL;
	g_autofree gchar *bus_path = NULL;
	gint bus_fd = -1;

	/* flashrom_programmer_init() mutates the programmer_args string. */
	temp = g_strdup_printf ("bus=%d", self->bus_number);
	fu_flashrom_device_set_programmer_args (flashrom_device, temp);

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
probe_active_flash_partition (FuFlashromLspconI2cSpiDevice *self,
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
fu_flashrom_lspcon_i2c_spi_device_set_version (FuDevice *device, GError **error)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	FuFlashromDevice *flashrom_device = FU_FLASHROM_DEVICE (device);
	FuFlashromContext *context = fu_flashrom_device_get_context (flashrom_device);
	const gchar *active_partition_name;
	g_autoptr(GBytes) contents = NULL;
	const guint8 *contents_buf;
	gsize contents_size;
	guint32 version_addr;
	g_autofree gchar *version = NULL;

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
fu_flashrom_lspcon_i2c_spi_device_setup (FuDevice *device, GError **error)
{
	const gchar *hw_id = NULL;
	g_autofree gchar *vid = NULL;
	g_autofree gchar *pid = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autofree gchar *instance_id = NULL;

	hw_id = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (device), "name", error);
	if (hw_id == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HID not found");
		return FALSE;
	}
	vid = g_strndup (hw_id, HID_LENGTH / 2);
	pid = g_strndup (&hw_id[HID_LENGTH / 2], HID_LENGTH / 2);
	vendor_id = g_strdup_printf ("I2C:%s", vid);
	fu_device_add_vendor_id (device, vendor_id);

	instance_id = g_strdup_printf ("FLASHROM-LSPCON-I2C-SPI\\VEN_%s&DEV_%s", vid, pid);
	fu_device_add_instance_id (device, instance_id);

	return fu_flashrom_lspcon_i2c_spi_device_set_version (device, error);
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_write_firmware (FuDevice *device,
						  FuFirmware *firmware,
						  FwupdInstallFlags flags,
						  GError **error)
{
	FuFlashromDevice *parent = FU_FLASHROM_DEVICE (device);
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	FuFlashromContext *context = fu_flashrom_device_get_context (parent);
	gsize flash_size = fu_flashrom_context_get_flash_size (context);
	guint8 *flash_contents_buf = g_malloc0 (flash_size);
	g_autoptr(GBytes) flash_contents = g_bytes_new_take (flash_contents_buf, flash_size);
	/* if the boot partition is active we could flash either, but prefer
	 * the first */
	const guint8 target_partition = self->active_partition == 1 ? 2 : 1;
	const gsize region_size = FLASH_REGIONS[target_partition].size;
	gsize fw_size;
	const guint8 *fw_buf;
	g_autoptr(GBytes) blob_fw = fu_firmware_get_bytes (firmware, error);
	if (blob_fw == NULL)
		return FALSE;

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
fu_flashrom_lspcon_i2c_spi_device_class_init (FuFlashromLspconI2cSpiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_flashrom_lspcon_i2c_spi_device_probe;
	klass_device->open = fu_flashrom_lspcon_i2c_spi_device_open;
	klass_device->setup = fu_flashrom_lspcon_i2c_spi_device_setup;
	klass_device->write_firmware = fu_flashrom_lspcon_i2c_spi_device_write_firmware;
	klass_device->reload = fu_flashrom_lspcon_i2c_spi_device_set_version;
}
