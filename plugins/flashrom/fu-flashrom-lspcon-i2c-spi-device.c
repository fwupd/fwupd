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
#include <libfwupdplugin/fu-hwids.h>

#define I2C_PATH_REGEX "/i2c-([0-9]+)/"
#define HID_LENGTH 8

struct _FuFlashromLspconI2cSpiDevice {
	FuFlashromDevice	  parent_instance;
	gint			  bus_number;
	guint8			  active_partition;
	gchar			 *aux_device_name;
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
	self->aux_device_name = NULL;

	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_flashrom_lspcon_i2c_spi_device_finalize (GObject *object)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (object);

	g_free (self->aux_device_name);
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
	g_return_val_if_fail(regex != NULL, FALSE);
	if (!g_regex_match_full (regex, path, -1, 0, 0, &info, error)) {
		return FALSE;
	}
	self->bus_number = g_ascii_strtoll ( g_match_info_fetch (info, 1), NULL, 10);

	return TRUE;
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_set_quirk_kv (FuDevice *device,
						const gchar *key,
						const gchar *value,
						GError **error)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);

	if (g_strcmp0 (key, "ParadeLspconAuxDeviceName") == 0) {
		self->aux_device_name = g_strdup (value);
		return TRUE;
	}
	return FU_DEVICE_CLASS (fu_flashrom_lspcon_i2c_spi_device_parent_class)
		->set_quirk_kv (device, key, value, error);
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_open (FuDevice *device,
					GError **error)
{
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	FuFlashromDevice *flashrom_device = FU_FLASHROM_DEVICE (device);
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

	/* do not chain up; that gets called in detach() instead */
	return TRUE;
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_close (FuDevice *device, GError **error)
{
	/* do not chain up, we avoided doing so for open() as well */
	return TRUE;
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
#ifdef HAVE_GUDEV
	FuFlashromLspconI2cSpiDevice *self = FU_FLASHROM_LSPCON_I2C_SPI_DEVICE (device);
	g_autoptr(GUdevClient) udev_client = g_udev_client_new (NULL);
	g_autoptr(GUdevEnumerator) enumerator = g_udev_enumerator_new (udev_client);
	g_autoptr(FuUdevDevice) aux_device = NULL;
	g_autoptr(FuDeviceLocker) aux_device_locker = NULL;
	GList *aux_devices;
	guint32 oui;
	guint8 version_buf[2];
	g_autofree gchar *version = NULL;

	/* determine active partition for flashing later */
	if (!probe_active_flash_partition (self, &self->active_partition, error))
		return FALSE;
	g_debug ("device reports running from partition %d", self->active_partition);
	if (self->active_partition < 1 || self->active_partition > 3) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "unexpected active flash partition: %d",
			     self->active_partition);
		return FALSE;
	}

	/* find the drm_dp_aux_dev specified by quirks that is connected to the
	 * LSPCON, in order to read DPCD from it */
	if (self->aux_device_name == NULL) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
				     "no DP aux device specified, unable to query LSPCON");
		return FALSE;
	}
	g_udev_enumerator_add_match_subsystem (enumerator, "drm_dp_aux_dev");
	g_udev_enumerator_add_match_sysfs_attr (enumerator, "name", self->aux_device_name);
	aux_devices = g_udev_enumerator_execute (enumerator);
	if (aux_devices == NULL) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to locate a DP aux device named \"%s\"",
			     self->aux_device_name);
		return FALSE;
	}
	if (g_list_length (aux_devices) > 1) {
		g_list_free_full (aux_devices, g_object_unref);
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "found multiple DP aux devices with name \"%s\"",
			     self->aux_device_name);
		return FALSE;
	}
	aux_device = fu_udev_device_new (g_steal_pointer (&aux_devices->data));
	g_list_free (aux_devices);
	g_debug ("using aux dev %s", fu_udev_device_get_sysfs_path (aux_device));
	/* open() requires the device have IDs set */
	if (!fu_udev_device_set_physical_id (aux_device, "drm_dp_aux_dev", error))
		return FALSE;

	if ((aux_device_locker = fu_device_locker_new (aux_device, error)) == NULL)
		return FALSE;
	/* DPCD address 00500-00502: device OUI */
	if (!fu_udev_device_pread_full (aux_device, 0x500, (guint8 *) &oui, 3, error))
		return FALSE;
	oui = GUINT32_FROM_BE(oui) >> 8;
	if (oui != 0x001CF8) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "device OUI %06X does not match expected value for Paradetech",
			     oui);
		return FALSE;
	}
	/* DPCD address 0x50A, 0x50B: branch device firmware
	 * major and minor revision */
	if (!fu_udev_device_pread_full (aux_device, 0x50a, version_buf,
					sizeof (version_buf), error))
		return FALSE;
	version = g_strdup_printf ("%d.%d", version_buf[0], version_buf[1]);
	fu_device_set_version (device, version);

	return TRUE;
#else
	g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "udev support is required to probe firmware version");
	return FALSE;
#endif /* HAVE_GUDEV */
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_setup (FuDevice *device, GError **error)
{
	const gchar *hw_id = NULL;
	const gchar *system_family = NULL;
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

	system_family = fu_context_get_hwid_value (fu_device_get_context (device),
						   FU_HWIDS_KEY_FAMILY);
	g_free (instance_id);
	instance_id = g_strdup_printf ("FLASHROM-LSPCON-I2C-SPI\\HwidFamily=%s",
				       system_family);
	fu_device_add_instance_id_full (device, instance_id,
					FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);

	return fu_flashrom_lspcon_i2c_spi_device_set_version (device, error);
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_detach (FuDevice *device, GError **error)
{
	return FU_DEVICE_CLASS (fu_flashrom_lspcon_i2c_spi_device_parent_class)
		->open (device, error);
}

static gboolean
fu_flashrom_lspcon_i2c_spi_device_attach (FuDevice *device, GError **error)
{
	return FU_DEVICE_CLASS (fu_flashrom_lspcon_i2c_spi_device_parent_class)
		->close (device, error);
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

	/* device (re)loads its firmware at power-on */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN);

	/* success */
	return TRUE;
}

static void
fu_flashrom_lspcon_i2c_spi_device_class_init (FuFlashromLspconI2cSpiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	GObjectClass *klass_object = G_OBJECT_CLASS (klass);

	klass_object->finalize = fu_flashrom_lspcon_i2c_spi_device_finalize;
	klass_device->set_quirk_kv = fu_flashrom_lspcon_i2c_spi_device_set_quirk_kv;
	klass_device->probe = fu_flashrom_lspcon_i2c_spi_device_probe;
	klass_device->open = fu_flashrom_lspcon_i2c_spi_device_open;
	klass_device->close = fu_flashrom_lspcon_i2c_spi_device_close;
	klass_device->setup = fu_flashrom_lspcon_i2c_spi_device_setup;
	klass_device->detach = fu_flashrom_lspcon_i2c_spi_device_detach;
	klass_device->attach = fu_flashrom_lspcon_i2c_spi_device_attach;
	klass_device->write_firmware = fu_flashrom_lspcon_i2c_spi_device_write_firmware;
	klass_device->reload = fu_flashrom_lspcon_i2c_spi_device_set_version;
}
