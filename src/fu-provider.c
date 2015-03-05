/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include "fu-cleanup.h"
#include "fu-common.h"
#include "fu-device.h"
#include "fu-pending.h"
#include "fu-provider-uefi.h"

static void     fu_provider_finalize	(GObject	*object);

#define FU_PROVIDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER, FuProviderPrivate))

#define FU_PROVIDER_FIRMWARE_MAX	(32 * 1024 * 1024)	/* bytes */

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (FuProvider, fu_provider, G_TYPE_OBJECT)

/**
 * fu_provider_coldplug:
 **/
gboolean
fu_provider_coldplug (FuProvider *provider, GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->coldplug != NULL)
		return klass->coldplug (provider, error);
	return TRUE;
}

/**
 * fu_provider_schedule_update:
 **/
static gboolean
fu_provider_schedule_update (FuDevice *device, GInputStream *stream, GError **error)
{
	gchar tmpname[] = {"XXXXXX.cap"};
	guint i;
	_cleanup_bytes_unref_ GBytes *fwbin = NULL;
	_cleanup_free_ gchar *filename = NULL;
	_cleanup_object_unref_ FuDevice *device_tmp = NULL;
	_cleanup_object_unref_ FuPending *pending = NULL;

	/* id already exists */
	pending = fu_pending_new ();
	device_tmp = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
	if (device_tmp != NULL) {
		g_set_error (error,
			     FU_ERROR,
			     FU_ERROR_ALREADY_SCHEDULED,
			     "%s is already scheduled to be updated",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* get a random filename */
	for (i = 0; i < 6; i++)
		tmpname[i] = g_random_int_range ('A', 'Z');
	filename = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", tmpname, NULL);

	/* just copy to the temp file */
	if (!g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_SET, NULL, error))
		return FALSE;
	fwbin = g_input_stream_read_bytes (stream,
					   FU_PROVIDER_FIRMWARE_MAX,
					   NULL, error);
	if (fwbin == NULL)
		return FALSE;
	if (!g_file_set_contents (filename,
				  g_bytes_get_data (fwbin, NULL),
				  g_bytes_get_size (fwbin),
				  error))
		return FALSE;

	/* schedule for next boot */
	g_debug ("schedule %s to be installed to %s on next boot",
		 filename, fu_device_get_id (device));
	fu_device_set_metadata (device, FU_DEVICE_KEY_FILENAME_CAB, filename);

	/* add to database */
	return fu_pending_add_device (pending, device, error);
}

/**
 * fu_provider_update:
 **/
gboolean
fu_provider_update (FuProvider *provider,
		    FuDevice *device,
		    GInputStream *stream_cab,
		    gint fd_fw,
		    FuProviderFlags flags,
		    GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	_cleanup_object_unref_ FuPending *pending = NULL;

	/* schedule for next reboot, or handle in the provider */
	if (flags & FU_PROVIDER_UPDATE_FLAG_OFFLINE) {
		if (klass->update_offline == NULL)
			return fu_provider_schedule_update (device,
							    stream_cab,
							    error);
		return klass->update_offline (provider, device, fd_fw, flags, error);
	}

	/* online */
	if (klass->update_online == NULL) {
		g_set_error_literal (error,
				     FU_ERROR,
				     FU_ERROR_NOT_POSSIBLE,
				     "No online update possible");
		return FALSE;
	}
	if (!klass->update_online (provider, device, fd_fw, flags, error))
		return FALSE;

	/* remove from pending database */
	pending = fu_pending_new ();
	return fu_pending_remove_device (pending, device, error);

}

/**
 * fu_provider_emit_added:
 **/
void
fu_provider_emit_added (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit added: %s", fu_device_get_id (device));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_ADDED], 0, device);
}

/**
 * fu_provider_emit_removed:
 **/
void
fu_provider_emit_removed (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit removed: %s", fu_device_get_id (device));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

/**
 * fu_provider_class_init:
 **/
static void
fu_provider_class_init (FuProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_provider_finalize;
	signals[SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
}

/**
 * fu_provider_init:
 **/
static void
fu_provider_init (FuProvider *provider)
{
}

/**
 * fu_provider_finalize:
 **/
static void
fu_provider_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_provider_parent_class)->finalize (object);
}
