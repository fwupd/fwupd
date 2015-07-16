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

#include <fwupd.h>
#include <appstream-glib.h>
#include <errno.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <string.h>

#include "fu-cleanup.h"
#include "fu-device.h"
#include "fu-pending.h"
#include "fu-provider-uefi.h"

static void     fu_provider_finalize	(GObject	*object);

#define FU_PROVIDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER, FuProviderPrivate))

#define FU_PROVIDER_FIRMWARE_MAX	(32 * 1024 * 1024)	/* bytes */

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (FuProvider, fu_provider, G_TYPE_OBJECT)

/**
 * fu_provider_offline_invalidate:
 **/
static gboolean
fu_provider_offline_invalidate (GError **error)
{
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ GFile *file1 = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	file1 = g_file_new_for_path (FU_OFFLINE_TRIGGER_FILENAME);
	if (!g_file_query_exists (file1, NULL))
		return TRUE;
	if (!g_file_delete (file1, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot delete %s: %s",
			     FU_OFFLINE_TRIGGER_FILENAME,
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_provider_offline_setup:
 **/
static gboolean
fu_provider_offline_setup (GError **error)
{
	gint rc;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create symlink for the systemd-system-update-generator */
	rc = symlink ("/var/lib", FU_OFFLINE_TRIGGER_FILENAME);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to create symlink %s to %s: %s",
			     FU_OFFLINE_TRIGGER_FILENAME,
			     "/var/lib", strerror (errno));
		return FALSE;
	}
	return TRUE;
}

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
fu_provider_schedule_update (FuProvider *provider,
			     FuDevice *device,
			     GInputStream *stream,
			     GError **error)
{
	gchar tmpname[] = {"XXXXXX.cap"};
	guint i;
	_cleanup_bytes_unref_ GBytes *fwbin = NULL;
	_cleanup_free_ gchar *dirname = NULL;
	_cleanup_free_ gchar *filename = NULL;
	_cleanup_object_unref_ FuDevice *device_tmp = NULL;
	_cleanup_object_unref_ FuPending *pending = NULL;
	_cleanup_object_unref_ GFile *file = NULL;

	/* id already exists */
	pending = fu_pending_new ();
	device_tmp = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
	if (device_tmp != NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_ALREADY_PENDING,
			     "%s is already scheduled to be updated",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* create directory */
	dirname = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	file = g_file_new_for_path (dirname);
	if (!g_file_query_exists (file, NULL)) {
		if (!g_file_make_directory_with_parents (file, NULL, error))
			return FALSE;
	}

	/* get a random filename */
	for (i = 0; i < 6; i++)
		tmpname[i] = g_random_int_range ('A', 'Z');
	filename = g_build_filename (dirname, tmpname, NULL);

	/* just copy to the temp file */
	fu_provider_set_status (provider, FWUPD_STATUS_SCHEDULING);
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
	if (!fu_pending_add_device (pending, device, error))
		return FALSE;

	/* next boot we run offline */
	return fu_provider_offline_setup (error);
}

/**
 * fu_provider_verify:
 **/
gboolean
fu_provider_verify (FuProvider *provider,
		    FuDevice *device,
		    FuProviderVerifyFlags flags,
		    GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->verify != NULL)
		return klass->verify (provider, device, flags, error);
	return TRUE;
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
	_cleanup_object_unref_ FuDevice *device_pending = NULL;
	GError *error_update = NULL;

	/* cancel the pending action */
	if (!fu_provider_offline_invalidate (error))
		return FALSE;

	/* try online first */
	if (klass->update_online != NULL) {
		pending = fu_pending_new ();
		device_pending = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
		if (!klass->update_online (provider, device, fd_fw, flags, &error_update)) {
			/* save the error to the database */
			if (device_pending != NULL) {
				fu_pending_set_error_msg (pending, device,
							  error_update->message, NULL);
			}
			g_propagate_error (error, error_update);
		}

		/* cleanup */
		if (error_update == NULL && device_pending != NULL) {
			const gchar *tmp;

			/* update pending database */
			fu_pending_set_state (pending, device, FU_PENDING_STATE_SUCCESS, NULL);

			/* delete cab file */
			tmp = fu_device_get_metadata (device_pending, FU_DEVICE_KEY_FILENAME_CAB);
			if (tmp != NULL && g_str_has_prefix (tmp, LIBEXECDIR)) {
				_cleanup_error_free_ GError *error_local = NULL;
				_cleanup_object_unref_ GFile *file = NULL;
				file = g_file_new_for_path (tmp);
				if (!g_file_delete (file, NULL, &error_local)) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "Failed to delete %s: %s",
						     tmp, error_local->message);
					return FALSE;
				}
			}
		}
		return TRUE;
	}

	/* schedule for next reboot, or handle in the provider */
	if (klass->update_offline == NULL)
		return fu_provider_schedule_update (provider,
						    device,
						    stream_cab,
						    error);
	return klass->update_offline (provider, device, fd_fw, flags, error);
}

/**
 * fu_provider_clear_results:
 **/
gboolean
fu_provider_clear_results (FuProvider *provider, FuDevice *device, GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ FuDevice *device_pending = NULL;
	_cleanup_object_unref_ FuPending *pending = NULL;

	/* handled by the provider */
	if (klass->clear_results != NULL)
		return klass->clear_results (provider, device, error);

	/* handled using the database */
	pending = fu_pending_new ();
	device_pending = fu_pending_get_device (pending,
						fu_device_get_id (device),
						&error_local);
	if (device_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* remove from pending database */
	return fu_pending_remove_device (pending, device, error);
}

/**
 * fu_provider_get_results:
 **/
gboolean
fu_provider_get_results (FuProvider *provider, FuDevice *device, GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	const gchar *tmp;
	guint i;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ FuDevice *device_pending = NULL;
	_cleanup_object_unref_ FuPending *pending = NULL;
	const gchar *copy_keys[] = {
		FU_DEVICE_KEY_PENDING_STATE,
		FU_DEVICE_KEY_PENDING_ERROR,
		FU_DEVICE_KEY_VERSION_OLD,
		FU_DEVICE_KEY_VERSION_NEW,
		NULL };

	/* handled by the provider */
	if (klass->get_results != NULL)
		return klass->get_results (provider, device, error);

	/* handled using the database */
	pending = fu_pending_new ();
	device_pending = fu_pending_get_device (pending,
						fu_device_get_id (device),
						&error_local);
	if (device_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* copy the important parts from the pending device to the real one */
	tmp = fu_device_get_metadata (device_pending, FU_DEVICE_KEY_PENDING_STATE);
	if (tmp == NULL || g_strcmp0 (tmp, "scheduled") == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Device %s has not been updated offline yet",
			     fu_device_get_id (device));
		return FALSE;
	}
	for (i = 0; copy_keys[i] != NULL; i++) {
		tmp = fu_device_get_metadata (device_pending, copy_keys[i]);
		if (tmp != NULL)
			fu_device_set_metadata (device, copy_keys[i], tmp);
	}
	return TRUE;
}

/**
 * fu_provider_device_add:
 **/
const gchar *
fu_provider_get_name (FuProvider *provider)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->get_name != NULL)
		return klass->get_name (provider);
	return NULL;
}

/**
 * fu_provider_device_add:
 **/
void
fu_provider_device_add (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit added: %s", fu_device_get_id (device));
	fu_device_set_metadata (device, FU_DEVICE_KEY_PROVIDER,
				fu_provider_get_name (provider));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_ADDED], 0, device);
}

/**
 * fu_provider_device_remove:
 **/
void
fu_provider_device_remove (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit removed: %s", fu_device_get_id (device));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

/**
 * fu_provider_set_status:
 **/
void
fu_provider_set_status (FuProvider *provider, FwupdStatus status)
{
	g_signal_emit (provider, signals[SIGNAL_STATUS_CHANGED], 0, status);
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
	signals[SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
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
