/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-chunk.h"
#include "fu-superio-common.h"
#include "fu-superio-it85-device.h"

struct _FuSuperioIt85Device {
	FuSuperioDevice		 parent_instance;
};

G_DEFINE_TYPE (FuSuperioIt85Device, fu_superio_it85_device, FU_TYPE_SUPERIO_DEVICE)

static gchar *
fu_superio_it85_device_get_str (FuSuperioDevice *self, guint8 idx, GError **error)
{
	GString *str = g_string_new (NULL);
	if (!fu_superio_device_ec_write1 (self, idx, error))
		return NULL;
	for (guint i = 0; i < 0xff; i++) {
		guint8 c = 0;
		if (!fu_superio_device_ec_read (self, &c, error))
			return NULL;
		if (c == '$')
			break;
		g_string_append_c (str, c);
	}
	return g_string_free (str, FALSE);
}

static gboolean
fu_superio_it85_device_setup (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	guint8 size_tmp = 0;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;

	/* FuSuperioDevice->setup */
	if (!FU_DEVICE_CLASS (fu_superio_it85_device_parent_class)->setup (device, error))
		return FALSE;

	/* get EC size */
	if (!fu_superio_device_ec_get_param (self, 0xe5, &size_tmp, error)) {
		g_prefix_error (error, "failed to get EC size: ");
		return FALSE;
	}
	fu_device_set_firmware_size (FU_DEVICE (self), ((guint32) size_tmp) << 10);

	/* get EC strings */
	name = fu_superio_it85_device_get_str (self, SIO_CMD_EC_GET_NAME_STR, error);
	if (name == NULL) {
		g_prefix_error (error, "failed to get EC name: ");
		return FALSE;
	}
	fu_device_set_name (FU_DEVICE (self), name);
	version = fu_superio_it85_device_get_str (self, SIO_CMD_EC_GET_VERSION_STR, error);
	if (version == NULL) {
		g_prefix_error (error, "failed to get EC version: ");
		return FALSE;
	}
	fu_device_set_version (FU_DEVICE (self), version);
	return TRUE;
}

static void
fu_superio_it85_device_init (FuSuperioIt85Device *self)
{
}

static void
fu_superio_it85_device_class_init (FuSuperioIt85DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->setup = fu_superio_it85_device_setup;
}
