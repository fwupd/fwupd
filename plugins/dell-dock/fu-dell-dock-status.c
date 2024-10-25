/*
 * Copyright 2018 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-dock-common.h"

struct _FuDellDockStatus {
	FuDevice parent_instance;
	guint64 blob_version_offset;
};

G_DEFINE_TYPE(FuDellDockStatus, fu_dell_dock_status, FU_TYPE_DEVICE)

static gchar *
fu_dell_dock_status_ver_string(guint32 status_version)
{
	/* guint32 BCD */
	return g_strdup_printf("%02x.%02x.%02x.%02x",
			       status_version & 0xff,
			       (status_version >> 8) & 0xff,
			       (status_version >> 16) & 0xff,
			       (status_version >> 24) & 0xff);
}

static gboolean
fu_dell_dock_status_setup(FuDevice *device, GError **error)
{
	FuDevice *parent;
	guint32 status_version;
	g_autofree gchar *dynamic_version = NULL;

	parent = fu_device_get_parent(device);
	status_version = fu_dell_dock_ec_get_status_version(parent);

	dynamic_version = fu_dell_dock_status_ver_string(status_version);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(device, dynamic_version);
	fu_device_set_logical_id(FU_DEVICE(device), "status");
	return TRUE;
}

static gboolean
fu_dell_dock_status_write(FuDevice *device,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDellDockStatus *self = FU_DELL_DOCK_STATUS(device);
	gsize length = 0;
	guint32 status_version = 0;
	const guint8 *data;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data(fw, &length);
	if (!fu_memcpy_safe((guint8 *)&status_version,
			    sizeof(status_version),
			    0x0, /* dst */
			    data,
			    length,
			    self->blob_version_offset, /* src */
			    sizeof(status_version),
			    error))
		return FALSE;
	dynamic_version = fu_dell_dock_status_ver_string(status_version);
	g_info("writing status firmware version %s", dynamic_version);

	if (!fu_dell_dock_ec_commit_package(fu_device_get_proxy(device), fw, error))
		return FALSE;

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(device, dynamic_version);
	return TRUE;
}

static gboolean
fu_dell_dock_status_open(FuDevice *device, GError **error)
{
	if (fu_device_get_proxy(device) == NULL) {
		if (fu_device_get_parent(device) == NULL) {
			g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no parent");
			return FALSE;
		}
		fu_device_set_proxy(device, fu_device_get_parent(device));
	}
	return fu_device_open(fu_device_get_proxy(device), error);
}

static gboolean
fu_dell_dock_status_close(FuDevice *device, GError **error)
{
	if (fu_device_get_proxy(device) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no proxy");
		return FALSE;
	}
	return fu_device_close(fu_device_get_proxy(device), error);
}

static gboolean
fu_dell_dock_status_set_quirk_kv(FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuDellDockStatus *self = FU_DELL_DOCK_STATUS(device);
	if (g_strcmp0(key, "DellDockBlobVersionOffset") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->blob_version_offset = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_dell_dock_status_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_dell_dock_status_parent_class)->finalize(object);
}

static void
fu_dell_dock_status_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 9, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_dell_dock_status_init(FuDellDockStatus *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.dock");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
}

static void
fu_dell_dock_status_class_init(FuDellDockStatusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_dock_status_finalize;
	device_class->write_firmware = fu_dell_dock_status_write;
	device_class->setup = fu_dell_dock_status_setup;
	device_class->open = fu_dell_dock_status_open;
	device_class->close = fu_dell_dock_status_close;
	device_class->set_quirk_kv = fu_dell_dock_status_set_quirk_kv;
	device_class->set_progress = fu_dell_dock_status_set_progress;
}

FuDellDockStatus *
fu_dell_dock_status_new(FuContext *ctx)
{
	FuDellDockStatus *self = NULL;
	self = g_object_new(FU_TYPE_DELL_DOCK_STATUS, "context", ctx, NULL);
	return self;
}
