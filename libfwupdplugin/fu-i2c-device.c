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

#include "fu-i2c-device.h"
#include "fu-string.h"
#include "fu-udev-device-private.h"

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
	g_autoptr(FuUdevDevice) udev_parent = NULL;

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
	udev_parent = FU_UDEV_DEVICE(fu_device_get_backend_parent_with_kind(device, "i2c", NULL));
	if (udev_parent != NULL) {
		g_autofree gchar *devfile = NULL;
		if (!fu_udev_device_parse_number(udev_parent, error))
			return FALSE;
		devfile =
		    g_strdup_printf("/dev/i2c-%u", (guint)fu_udev_device_get_number(udev_parent));
		fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), devfile);
	}

	/* success */
	return TRUE;
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
}

static void
fu_i2c_device_class_init(FuI2cDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_i2c_device_probe;
}
