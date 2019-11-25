/*
 * Copyright (C) 2018 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-dock-common.h"

struct _FuDellDockStatus {
	FuDevice			 parent_instance;
	guint64				 blob_version_offset;
};

G_DEFINE_TYPE (FuDellDockStatus, fu_dell_dock_status, FU_TYPE_DEVICE)

static gchar *
fu_dell_dock_status_ver_string (guint32 status_version)
{
	/* guint32 BCD */
	return g_strdup_printf ("%02x.%02x.%02x.%02x",
				status_version & 0xff,
				(status_version >> 8) & 0xff,
				(status_version >> 16) & 0xff,
				(status_version >> 24) & 0xff);
}

static gboolean
fu_dell_dock_status_setup (FuDevice *device, GError **error)
{
	FuDevice *parent;
	guint32 status_version;
	g_autofree gchar *dynamic_version = NULL;

	parent = fu_device_get_parent (device);
	status_version = fu_dell_dock_ec_get_status_version (parent);

	dynamic_version = fu_dell_dock_status_ver_string (status_version);
	fu_device_set_version (device, dynamic_version, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_logical_id (FU_DEVICE (device), "status");

	fu_dell_dock_clone_updatable (device);

	return TRUE;
}

static gboolean
fu_dell_dock_status_write (FuDevice *device,
			   FuFirmware *firmware,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuDellDockStatus *self = FU_DELL_DOCK_STATUS (device);
	FuDevice *parent;
	gsize length = 0;
	guint32 status_version = 0;
	const guint8 *data;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (FU_IS_FIRMWARE (firmware), FALSE);

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data (fw, &length);
	if (!fu_memcpy_safe ((guint8 *) &status_version, sizeof(status_version), 0x0,	/* dst */
			     data, length, self->blob_version_offset,		/* src */
			     sizeof(status_version), error))
		return FALSE;
	dynamic_version = fu_dell_dock_status_ver_string (status_version);
	g_debug ("writing status firmware version %s", dynamic_version);

	parent = fu_device_get_parent (device);
	if (!fu_dell_dock_ec_commit_package (parent, fw, error))
		return FALSE;

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_version (device, dynamic_version, FWUPD_VERSION_FORMAT_QUAD);
	return TRUE;
}

static gboolean
fu_dell_dock_status_open (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);

	g_return_val_if_fail (parent != NULL, FALSE);

	return fu_device_open (parent, error);
}

static gboolean
fu_dell_dock_status_close (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);

	return fu_device_close (parent, error);
}

static gboolean
fu_dell_dock_status_set_quirk_kv (FuDevice *device,
				  const gchar *key,
				  const gchar *value,
				  GError **error)
{
	FuDellDockStatus *self = FU_DELL_DOCK_STATUS (device);
	if (g_strcmp0 (key, "DellDockBlobVersionOffset") == 0) {
		self->blob_version_offset = fu_common_strtoull (value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_dell_dock_status_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_dell_dock_status_parent_class)->finalize (object);
}

static void
fu_dell_dock_status_init (FuDellDockStatus *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.dell.dock");
}

static void
fu_dell_dock_status_class_init (FuDellDockStatusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_dell_dock_status_finalize;
	klass_device->write_firmware = fu_dell_dock_status_write;
	klass_device->setup = fu_dell_dock_status_setup;
	klass_device->open = fu_dell_dock_status_open;
	klass_device->close = fu_dell_dock_status_close;
	klass_device->set_quirk_kv = fu_dell_dock_status_set_quirk_kv;
}

FuDellDockStatus *
fu_dell_dock_status_new (void)
{
	FuDellDockStatus *self = NULL;
	self = g_object_new (FU_TYPE_DELL_DOCK_STATUS, NULL);
	return self;
}
