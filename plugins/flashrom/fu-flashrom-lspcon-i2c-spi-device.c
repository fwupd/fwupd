/*
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-device.h"
#include "fu-flashrom-lspcon-i2c-spi-device.h"

#include <libflashrom.h>

#define HID_LENGTH 8
#define DEVICE_GUID_FORMAT "FLASHROM-LSPCON-I2C-SPI\\VEN_%s&DEV_%s"

struct _FuFlashromLspconI2cSpiDevice {
	FuFlashromDevice	  parent_instance;
	gchar			 *bus_path;
	guint8			  active_partition;
};

G_DEFINE_TYPE (FuFlashromLspconI2cSpiDevice, fu_flashrom_lspcon_i2c_spi_device,
	       FU_TYPE_FLASHROM_DEVICE)

struct romentry {
	guint32			 start;
	guint32			 end;
	gboolean		 included;
	const gchar		*name;
	const gchar		*file;
};

struct flashrom_layout {
	struct romentry		*entries;
	gsize			 num_entries;
};

static struct romentry entries[5] = {
	{ .start = 0x00002, .end = 0x00003, .name = "FLAG" },
	{ .start = 0x10000, .end = 0x1ffff, .name = "PAR1" },
	{ .start = 0x20000, .end = 0x2ffff, .name = "PAR2" },
	{ .start = 0x15000, .end = 0x15002, .name = "VER1" },
	{ .start = 0x25000, .end = 0x25002, .name = "VER2" },
};

static struct flashrom_layout layout = {
	.entries = entries, .num_entries = sizeof(entries) / sizeof(entries[0])
};

static void
fu_flashrom_lspcon_i2c_spi_device_finalize(GObject *object)
{
	FuFlashromLspconI2cSpiDevice *self =
		FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (object);

	g_free (self->bus_path);
	G_OBJECT_CLASS (fu_flashrom_lspcon_i2c_spi_device_parent_class)->finalize (object);
}

static void
fu_flashrom_lspcon_i2c_spi_device_init (FuFlashromLspconI2cSpiDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_probe (FuDevice *device, GError **error)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	FuFlashromDevice *flashrom_device = FU_FLASHROM_DEVICE (device);
	FuDeviceClass *klass =
		FU_DEVICE_CLASS (fu_flashrom_lspcon_i2c_spi_device_parent_class);
	g_autoptr(GPtrArray) sibling_buses = NULL;

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

	/* locate an I2C bus that is a sibling of this device, assumed to be
	 * the bus the device is connected to */
	sibling_buses = fu_udev_device_get_siblings_with_subsystem (FU_UDEV_DEVICE (self), "i2c-dev");
	for (guint i = 0; i < sibling_buses->len; i++) {
		FuUdevDevice *bus_device = g_ptr_array_index (sibling_buses, i);
		const gchar *bus_path = fu_udev_device_get_device_file (bus_device);

		if (bus_path == NULL) {
			g_info ("ignoring bus %s with no device file",
			        fu_udev_device_get_sysfs_path (bus_device));
			continue;
		}

		self->bus_path = g_strdup (bus_path);
		/* if there are other buses, ignore them (because we already
		 * have one) but note they exist */
		if (i < (sibling_buses->len - 1))
			g_debug ("%u additional buses found but ignored",
				 sibling_buses->len - i - 1);
		break;
	}

	if (self->bus_path == NULL) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
			     "Failed to identify I2C bus corresponding to %s",
			     fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (self)));
		return FALSE;
	}
	return TRUE;
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

	/* flashrom_programmer_init() mutates the programmer_args string. */
	temp = g_strdup_printf ("devpath=%s", self->bus_path);
	fu_flashrom_device_set_programmer_args (flashrom_device, temp);

	return klass->open (device, error);
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_set_version (FuDevice *device, GError **error)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	FuFlashromDevice *flashrom_device = FU_FLASHROM_DEVICE (device);
	g_autofree gchar *contents = NULL;
	g_autofree gchar *version = NULL;
	struct flashrom_flashctx *flashctx = NULL;
	gsize flash_size;
	guint32 addr;

	/* set up flashrom layout */
	flashctx = fu_flashrom_device_get_flashctx (flashrom_device);
	flashrom_layout_set (flashctx, &layout);

	/* only include flag and version regions on layout */
	entries[0].included = TRUE;
	entries[1].included = FALSE;
	entries[2].included = FALSE;
	entries[3].included = TRUE;
	entries[4].included = TRUE;

	/* read the current flash contents */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	flash_size = fu_flashrom_device_get_flash_size (flashrom_device);
	contents = g_malloc0 (flash_size);
	if (flashrom_image_read (flashctx, contents, flash_size)) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_READ,
				     "failed to read flash contents");
		return FALSE;
	}

	/* grab the active partition */
	self->active_partition = contents[entries[0].start];

	if (self->active_partition != 1 && self->active_partition != 2) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_READ,
				     "failed to read active partition");
		return FALSE;
	}

	/* find the current version for the active partition */
	addr = entries[self->active_partition + 2].start;
	version = g_strdup_printf ("%d.%d", contents[addr], contents[addr + 2]);
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
	g_autofree gchar *temp = NULL;
	g_autofree gchar *guid = NULL;

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

	temp = g_strdup_printf (DEVICE_GUID_FORMAT, vid, pid);
	fu_device_add_instance_id (device, temp);
	guid = fwupd_guid_hash_string (temp);
	fu_device_add_guid (device, guid);

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
	struct flashrom_flashctx *flashctx = fu_flashrom_device_get_flashctx (parent);
	gsize flash_size = fu_flashrom_device_get_flash_size (parent);
	g_autofree guint8 *newcontents = g_malloc0 (flash_size);
	const guint8 target_partition = self->active_partition == 1 ? 2 : 1;
	const gsize region_size = entries[target_partition].end -
				  entries[target_partition].start + 1;
	gsize sz = 0;
	gint rc;
	const guint8 *buf;
	g_autoptr(GBytes) blob_fw = fu_firmware_get_bytes (firmware, error);
	if (blob_fw == NULL)
		return FALSE;

	buf = g_bytes_get_data (blob_fw, &sz);

	if (sz != region_size) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid image size 0x%x, expected 0x%x",
			     (guint) sz, (guint) flash_size);
		return FALSE;
	}
	if (!fu_memcpy_safe (newcontents, flash_size,
			     entries[target_partition].start, buf, sz, 0,
			     region_size, error))
		return FALSE;

	flashrom_flag_set (flashctx, FLASHROM_FLAG_VERIFY_AFTER_WRITE, TRUE);
	flashrom_layout_set (flashctx, &layout);

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	fu_device_set_progress (device, 0); /* urgh */

	/* only include target_partition on layout */
	entries[0].included = FALSE;
	entries[3].included = FALSE;
	entries[4].included = FALSE;
	entries[self->active_partition].included = FALSE;
	entries[target_partition].included = TRUE;

	/* write target_partition */
	rc = flashrom_image_write (flashctx, (void *) newcontents, flash_size,
				   NULL /* refbuffer */);
	if (rc != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "image write failed, err=%i", rc);
		return FALSE;
	}

	/* only include flag area on layout */
	entries[0].included = TRUE;
	entries[target_partition].included = FALSE;

	/* Flag area is header bytes (0x55, 0xAA) followed by the bank ID to
	 * boot from (1 or 2) and the two's complement inverse of that bank ID
	 * (0 or 0xFF). We only write bytes 2 and 3, assuming the header is
	 * already valid. */
	newcontents[2] = (gint8) target_partition;
	newcontents[3] = (gint8) -target_partition + 1;

	/* write flag area */
	rc = flashrom_image_write (flashctx, (void *) newcontents, flash_size,
				   NULL /* refbuffer */);
	if (rc != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "flag write failed, err=%i", rc);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_flashrom_lspcon_i2c_spi_device_class_init (FuFlashromLspconI2cSpiDeviceClass *klass)
{
	GObjectClass *klass_object = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);

	klass_object->finalize = fu_flashrom_lspcon_i2c_spi_device_finalize;
	klass_device->probe = fu_flashrom_lspcon_i2c_spi_device_probe;
	klass_device->open = fu_flashrom_lspcon_i2c_spi_device_open;
	klass_device->setup = fu_flashrom_lspcon_i2c_spi_device_setup;
	klass_device->write_firmware = fu_flashrom_lspcon_i2c_spi_device_write_firmware;
	klass_device->reload = fu_flashrom_lspcon_i2c_spi_device_set_version;
}
