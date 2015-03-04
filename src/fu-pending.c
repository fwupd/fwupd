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
#include <sqlite3.h>

#include "fu-cleanup.h"
#include "fu-common.h"
#include "fu-pending.h"

static void fu_pending_finalize			 (GObject *object);

#define FU_PENDING_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PENDING, FuPendingPrivate))

/**
 * FuPendingPrivate:
 *
 * Private #FuPending data
 **/
struct _FuPendingPrivate
{
	sqlite3				*db;
};

G_DEFINE_TYPE (FuPending, fu_pending, G_TYPE_OBJECT)

/**
 * fu_pending_load:
 **/
static gboolean
fu_pending_load (FuPending *pending, GError **error)
{
	char *error_msg = NULL;
	const char *statement;
	gint rc;
	_cleanup_free_ gchar *filename = NULL;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);
	g_return_val_if_fail (pending->priv->db == NULL, FALSE);

	filename = g_build_filename (LOCALSTATEDIR, "lib", "fwupd",
				     "pending.db", NULL);
	g_debug ("FuPending: trying to open database '%s'", filename);
	rc = sqlite3_open (filename, &pending->priv->db);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FU_ERROR,
			     FU_ERROR_INTERNAL,
			     "Can't open %s: %s",
			     filename, sqlite3_errmsg (pending->priv->db));
		sqlite3_close (pending->priv->db);
		return FALSE;
	}

	/* check devices */
	rc = sqlite3_exec (pending->priv->db, "SELECT * FROM pending LIMIT 1",
			   NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("FuPending: creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE pending ("
			    "device_id TEXT PRIMARY KEY,"
			    "filename TEXT,"
			    "display_name TEXT,"
			    "version_old TEXT,"
			    "version_new TEXT);";
		rc = sqlite3_exec (pending->priv->db, statement, NULL, NULL, &error_msg);
		if (rc != SQLITE_OK) {
			g_set_error (error,
				     FU_ERROR,
				     FU_ERROR_INTERNAL,
				     "Cannot create database: %s",
				     error_msg);
			sqlite3_free (error_msg);
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * fu_pending_add_device:
 **/
gboolean
fu_pending_add_device (FuPending *pending, FuDevice *device, GError **error)
{
	char *error_msg = NULL;
	char *statement;
	gboolean ret = TRUE;
	gint rc;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);

	/* lazy load */
	if (pending->priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	g_debug ("FuPending: add device %s", fu_device_get_id (device));
	statement = sqlite3_mprintf ("INSERT INTO pending (device_id,"
							  "filename,"
							  "display_name,"
							  "version_old,"
							  "version_new) "
				     "VALUES ('%q','%q','%q','%q','%q')",
				     fu_device_get_id (device),
				     fu_device_get_metadata (device, FU_DEVICE_KEY_FILENAME_CAB),
				     fu_device_get_metadata (device, FU_DEVICE_KEY_DISPLAY_NAME),
				     fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION),
				     fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_NEW));

	/* insert entry */
	rc = sqlite3_exec (pending->priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FU_ERROR,
			     FU_ERROR_INTERNAL,
			     "SQL error: %s",
			     error_msg);
		sqlite3_free (error_msg);
		ret = FALSE;
		goto out;
	}
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * fu_pending_remove_device:
 **/
gboolean
fu_pending_remove_device (FuPending *pending, FuDevice *device, GError **error)
{
	char *error_msg = NULL;
	char *statement;
	gboolean ret = TRUE;
	gint rc;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);

	/* lazy load */
	if (pending->priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	g_debug ("FuPending: remove device %s", fu_device_get_id (device));
	statement = sqlite3_mprintf ("DELETE FROM pending WHERE "
				     "device_id = '%q';",
				     fu_device_get_id (device));

	/* remove entry */
	rc = sqlite3_exec (pending->priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FU_ERROR,
			     FU_ERROR_INTERNAL,
			     "SQL error: %s",
			     error_msg);
		sqlite3_free (error_msg);
		ret = FALSE;
		goto out;
	}
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * fu_pending_device_sqlite_cb:
 **/
static gint
fu_pending_device_sqlite_cb (void *data,
			gint argc,
			gchar **argv,
			gchar **col_name)
{
	GPtrArray *array = (GPtrArray *) data;
	FuDevice *device;
	gint i;

	/* create new result */
	device = fu_device_new ();
	g_ptr_array_add (array, device);

	g_debug ("FuPending: got sql result %s", argv[0]);
	for (i = 0; i < argc; i++) {
		if (g_strcmp0 (col_name[i], "device_id") == 0) {
			fu_device_set_id (device, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "filename") == 0) {
			fu_device_set_metadata (device, FU_DEVICE_KEY_FILENAME_CAB, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "display_name") == 0) {
			fu_device_set_metadata (device, FU_DEVICE_KEY_DISPLAY_NAME, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "version_old") == 0) {
			fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "version_new") == 0) {
			fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION_NEW, argv[i]);
			continue;
		}
		g_warning ("unhandled %s=%s", col_name[i], argv[i]);
	}

	return 0;
}

/**
 * fu_pending_get_device:
 **/
FuDevice *
fu_pending_get_device (FuPending *pending, const gchar *device_id, GError **error)
{
	FuDevice *device = NULL;
	char *error_msg = NULL;
	char *statement;
	gint rc;
	_cleanup_ptrarray_unref_ GPtrArray *array_tmp = NULL;

	g_return_val_if_fail (FU_IS_PENDING (pending), NULL);

	/* lazy load */
	if (pending->priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	/* get all the devices */
	g_debug ("FuPending: get device");
	statement = sqlite3_mprintf ("SELECT * FROM pending WHERE "
				     "device_id = '%q';",
				     device_id);
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	rc = sqlite3_exec (pending->priv->db,
			   statement,
			   fu_pending_device_sqlite_cb,
			   array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FU_ERROR,
			     FU_ERROR_INTERNAL,
			     "SQL error: %s",
			     error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
	if (array_tmp->len == 0) {
		g_set_error_literal (error,
				     FU_ERROR,
				     FU_ERROR_INTERNAL,
				     "No devices found");
		goto out;
	}
	device = g_object_ref (g_ptr_array_index (array_tmp, 0));
out:
	sqlite3_free (statement);
	return device;
}

/**
 * fu_pending_get_devices:
 **/
GPtrArray *
fu_pending_get_devices (FuPending *pending, GError **error)
{
	GPtrArray *array = NULL;
	char *error_msg = NULL;
	char *statement;
	gint rc;
	_cleanup_ptrarray_unref_ GPtrArray *array_tmp = NULL;

	g_return_val_if_fail (FU_IS_PENDING (pending), NULL);

	/* lazy load */
	if (pending->priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	/* get all the devices */
	g_debug ("FuPending: get devices");
	statement = sqlite3_mprintf ("SELECT * FROM pending;");
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	rc = sqlite3_exec (pending->priv->db,
			   statement,
			   fu_pending_device_sqlite_cb,
			   array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FU_ERROR,
			     FU_ERROR_INTERNAL,
			     "SQL error: %s",
			     error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	sqlite3_free (statement);
	return array;
}

/**
 * fu_pending_class_init:
 **/
static void
fu_pending_class_init (FuPendingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_pending_finalize;
	g_type_class_add_private (klass, sizeof (FuPendingPrivate));
}

/**
 * fu_pending_init:
 **/
static void
fu_pending_init (FuPending *pending)
{
	pending->priv = FU_PENDING_GET_PRIVATE (pending);
}

/**
 * fu_pending_finalize:
 **/
static void
fu_pending_finalize (GObject *object)
{
	FuPending *pending = FU_PENDING (object);
	FuPendingPrivate *priv = pending->priv;

	if (priv->db != NULL)
		sqlite3_close (priv->db);

	G_OBJECT_CLASS (fu_pending_parent_class)->finalize (object);
}

/**
 * fu_pending_new:
 **/
FuPending *
fu_pending_new (void)
{
	FuPending *pending;
	pending = g_object_new (FU_TYPE_PENDING, NULL);
	return FU_PENDING (pending);
}
