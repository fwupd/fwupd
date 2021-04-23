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

static const struct romentry ENTRIES_TEMPLATE[6] = {
	{ .start = 0x00002, .end = 0x00003, .included = FALSE, .name = "FLAG" },
	{ .start = 0x10000, .end = 0x1ffff, .included = FALSE, .name = "PAR1" },
	{ .start = 0x20000, .end = 0x2ffff, .included = FALSE, .name = "PAR2" },
	{ .start = 0x15000, .end = 0x15002, .included = FALSE, .name = "VER1" },
	{ .start = 0x25000, .end = 0x25002, .included = FALSE, .name = "VER2" },
	{ .start = 0x35000, .end = 0x35002, .included = FALSE, .name = "VERBOOT" },
};

typedef struct flashrom_layout FlashromLayout;

static FlashromLayout *
create_flash_layout (void)
{
	struct flashrom_layout *out = g_new (FlashromLayout, 1);
#if GLIB_CHECK_VERSION(2,67,3)
	out->entries = g_memdup2 (ENTRIES_TEMPLATE, sizeof (ENTRIES_TEMPLATE));
#else
	out->entries = g_memdup (ENTRIES_TEMPLATE, sizeof (ENTRIES_TEMPLATE));
#endif
	out->num_entries = sizeof (ENTRIES_TEMPLATE) / sizeof (*ENTRIES_TEMPLATE);
	return out;
}

static void
dispose_flash_layout (FlashromLayout *layout) {
	g_free (layout->entries);
	g_free (layout);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FlashromLayout, dispose_flash_layout);


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
	g_autofree gchar *contents = NULL;
	g_autofree gchar *version = NULL;
	struct flashrom_flashctx *flashctx = NULL;
	g_autoptr(FlashromLayout) layout = create_flash_layout ();
	gsize flash_size;
	guint32 addr;

	/* set up flashrom layout */
	flashctx = fu_flashrom_device_get_flashctx (flashrom_device);
	flashrom_layout_set (flashctx, layout);

	/* get the active partition */
	if (!probe_active_flash_partition (self, &self->active_partition, error))
		return FALSE;
	g_debug ("device reports running from partition %d", self->active_partition);
	if (self->active_partition < 1 || self->active_partition > 3) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_BROKEN_SYSTEM,
			     "Unexpected active flash partition: %d",
			     self->active_partition);
		return FALSE;
	}

	/* read version bytes for the active partition from device flash */
	layout->entries[self->active_partition + 2].included = TRUE;

	/* read the current flash contents */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	flash_size = fu_flashrom_device_get_flash_size (flashrom_device);
	contents = g_malloc0 (flash_size);
	if (flashrom_image_read (flashctx, contents, flash_size)) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_READ,
				     "failed to read flash contents");
		return FALSE;
	}

	/* extract the active partition's version number */
	addr = layout->entries[self->active_partition + 2].start;
	g_return_val_if_fail (addr < (flash_size - 2), FALSE);
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
	struct flashrom_flashctx *flashctx = fu_flashrom_device_get_flashctx (parent);
	gsize flash_size = fu_flashrom_device_get_flash_size (parent);
	g_autofree guint8 *newcontents = g_malloc0 (flash_size);
	g_autoptr(FlashromLayout) layout = create_flash_layout ();
	/* if the boot partition is active we could flash either, but prefer
	 * the first */
	const guint8 target_partition = self->active_partition == 1 ? 2 : 1;
	const gsize region_size = layout->entries[target_partition].end -
				  layout->entries[target_partition].start + 1;
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
			     layout->entries[target_partition].start, buf, sz, 0,
			     region_size, error))
		return FALSE;

	flashrom_flag_set (flashctx, FLASHROM_FLAG_VERIFY_AFTER_WRITE, TRUE);
	flashrom_layout_set (flashctx, layout);

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	fu_device_set_progress (device, 0); /* urgh */

	/* write target_partition only */
	layout->entries[target_partition].included = TRUE;
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
	layout->entries[0].included = TRUE;
	layout->entries[target_partition].included = FALSE;

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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_flashrom_lspcon_i2c_spi_device_probe;
	klass_device->open = fu_flashrom_lspcon_i2c_spi_device_open;
	klass_device->setup = fu_flashrom_lspcon_i2c_spi_device_setup;
	klass_device->write_firmware = fu_flashrom_lspcon_i2c_spi_device_write_firmware;
	klass_device->reload = fu_flashrom_lspcon_i2c_spi_device_set_version;
}
