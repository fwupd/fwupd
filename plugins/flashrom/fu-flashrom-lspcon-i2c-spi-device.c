/*
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-device.h"
#include "fu-flashrom-lspcon-i2c-spi-device.h"

#include <libflashrom.h>

#define I2C_PATH_REGEX "/i2c-([0-9]+)/"
#define HID_LENGTH 8
#define DEVICE_GUID_FORMAT "FLASHROM-LSPCON-I2C-SPI\\VEN_%s&DEV_%s"

struct _FuFlashromLspconI2cSpiDevice {
	FuFlashromDevice	  parent_instance;
	gint			  bus_number;
};

G_DEFINE_TYPE (FuFlashromLspconI2cSpiDevice, fu_flashrom_lspcon_i2c_spi_device,
	       FU_TYPE_FLASHROM_DEVICE)

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

	/* flashrom_programmer_init() mutates the programmer_args string. */
	temp = g_strdup_printf ("bus=%d", self->bus_number);
	fu_flashrom_device_set_programmer_args (flashrom_device, temp);

	return klass->open (device, error);
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

	/* TODO: Get the real version number. */
	fu_device_set_version (device, "0.0");

	return TRUE;
}

static void
fu_flashrom_lspcon_i2c_spi_device_class_init (FuFlashromLspconI2cSpiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_flashrom_lspcon_i2c_spi_device_probe;
	klass_device->open = fu_flashrom_lspcon_i2c_spi_device_open;
	klass_device->setup = fu_flashrom_lspcon_i2c_spi_device_setup;
}
