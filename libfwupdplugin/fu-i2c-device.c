/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuI2cDevice"

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_I2C_DEV_H
#include <linux/i2c-dev.h>
#endif

#include "fu-device-private.h"
#include "fu-i2c-device.h"
#include "fu-string.h"
#include "fu-udev-device-private.h"

#define FU_I2C_DEVICE_IOCTL_TIMEOUT 2000

/**
 * FuI2cDevice
 *
 * A I²C device with an assigned bus number.
 *
 * See also: #FuUdevDevice
 */

G_DEFINE_TYPE(FuI2cDevice, fu_i2c_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_i2c_device_probe(FuDevice *device, GError **error)
{
	FuI2cDevice *self = FU_I2C_DEVICE(device);
	g_autofree gchar *attr_name = NULL;

	/* FuUdevDevice */
	if (!FU_DEVICE_CLASS(fu_i2c_device_parent_class)->probe(device, error))
		return FALSE;

	/* set physical ID */
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "i2c", error))
		return FALSE;

	/* i2c devices all expose a name */
	attr_name = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					      "name",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      error);
	if (attr_name == NULL)
		return FALSE;
	fu_device_add_instance_strsafe(device, "NAME", attr_name);
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_GENERIC |
						  FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "I2C",
					      "NAME",
					      NULL))
		return FALSE;

	/* get bus number out of sysfs path */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "i2c") != 0) {
		g_autoptr(FuUdevDevice) udev_parent = FU_UDEV_DEVICE(
		    fu_device_get_backend_parent_with_subsystem(device, "i2c", NULL));
		if (udev_parent != NULL) {
			if (!fu_udev_device_parse_number(udev_parent, error))
				return FALSE;
			fu_udev_device_set_number(FU_UDEV_DEVICE(self),
						  fu_udev_device_get_number(udev_parent));
		}
	}

	/* set the device file manually */
	if (fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)) == NULL) {
		const gchar *sysfs = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
		g_auto(GStrv) tokens = fu_strsplit(sysfs, strlen(sysfs), "/", -1);
		g_autofree gchar *devfile = NULL;
		guint64 number = G_MAXUINT64;

		for (guint i = 0; tokens[i] != NULL; i++) {
			if (!g_str_has_prefix(tokens[i], "i2c-"))
				continue;

			if (!fu_strtoull(tokens[i] + 4,
					 &number,
					 0x0,
					 G_MAXUINT64,
					 FU_INTEGER_BASE_AUTO,
					 error))
				return FALSE;
			break;
		}
		if (number == G_MAXUINT64) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Could not find i2c bus number in sysfs path");
			return FALSE;
		}
		fu_udev_device_set_number(FU_UDEV_DEVICE(self), number);
		devfile = g_strdup_printf("/dev/i2c-%" G_GUINT64_FORMAT, number);
		fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), devfile);
	}

	/* i2c devices are often tied to the platform, and usually have very unhelpful names */
	if (!fu_device_has_private_flag(FU_DEVICE(self),
					FU_I2C_DEVICE_PRIVATE_FLAG_NO_HWID_GUIDS)) {
		GPtrArray *hwid_guids = fu_context_get_hwid_guids(fu_device_get_context(device));
		for (guint i = 0; i < hwid_guids->len; i++) {
			const gchar *hwid_guid = g_ptr_array_index(hwid_guids, i);
			fu_device_add_instance_str(device, "HWID", hwid_guid);
			fu_device_build_instance_id_full(device,
							 FU_DEVICE_INSTANCE_FLAG_GENERIC |
							     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
							 NULL,
							 "I2C",
							 "NAME",
							 "HWID",
							 NULL);
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_i2c_device_set_address:
 * @self: a #FuI2cDevice
 * @address: address
 * @force: Force the address, even if the device is device is busy (typically with a kernel driver)
 * @error: (nullable): optional return location for an error
 *
 * Sets the I²C device address.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_i2c_device_set_address(FuI2cDevice *self, guint8 address, gboolean force, GError **error)
{
#ifdef HAVE_I2C_DEV_H
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	if (!fu_ioctl_execute(ioctl,
			      force ? I2C_SLAVE_FORCE : I2C_SLAVE,
			      (guint8 *)(guintptr)address,
			      sizeof(guintptr),
			      NULL,
			      FU_I2C_DEVICE_IOCTL_TIMEOUT,
			      FU_IOCTL_FLAG_NONE,
			      error)) {
		g_prefix_error(error, "failed to set address 0x%02x: ", address);
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <linux/i2c-dev.h> not found");
	return FALSE;
#endif
}

/**
 * fu_i2c_device_write:
 * @self: a #FuI2cDevice
 * @buf: (out): data
 * @bufsz: size of @data
 * @error: (nullable): optional return location for an error
 *
 * Write multiple bytes to the I²C device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_i2c_device_write(FuI2cDevice *self, const guint8 *buf, gsize bufsz, GError **error)
{
	return fu_udev_device_pwrite(FU_UDEV_DEVICE(self), 0x0, buf, bufsz, error);
}

/**
 * fu_i2c_device_read:
 * @self: a #FuI2cDevice
 * @buf: (out): data
 * @bufsz: size of @data
 * @error: (nullable): optional return location for an error
 *
 * Read multiple bytes from the I²C device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_i2c_device_read(FuI2cDevice *self, guint8 *buf, gsize bufsz, GError **error)
{
	return fu_udev_device_pread(FU_UDEV_DEVICE(self), 0x0, buf, bufsz, error);
}

static void
fu_i2c_device_init(FuI2cDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_I2C_DEVICE_PRIVATE_FLAG_NO_HWID_GUIDS);
}

static void
fu_i2c_device_class_init(FuI2cDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_i2c_device_probe;
}
