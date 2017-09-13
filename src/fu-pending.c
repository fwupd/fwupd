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
#include <glib-object.h>
#include <gio/gio.h>
#include <sqlite3.h>
#include <stdlib.h>

#include "fu-device-private.h"
#include "fu-pending.h"

static void fu_pending_finalize			 (GObject *object);

typedef struct {
	sqlite3				*db;
} FuPendingPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuPending, fu_pending, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_pending_get_instance_private (o))

static gboolean
fu_pending_load (FuPending *pending, GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	char *error_msg = NULL;
	const char *statement;
	gint rc;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);
	g_return_val_if_fail (priv->db == NULL, FALSE);

	/* create directory */
	dirname = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	file = g_file_new_for_path (dirname);
	if (!g_file_query_exists (file, NULL)) {
		if (!g_file_make_directory_with_parents (file, NULL, error))
			return FALSE;
	}

	/* open */
	filename = g_build_filename (dirname, "pending.db", NULL);
	g_debug ("FuPending: trying to open database '%s'", filename);
	rc = sqlite3_open (filename, &priv->db);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "Can't open %s: %s",
			     filename, sqlite3_errmsg (priv->db));
		sqlite3_close (priv->db);
		return FALSE;
	}

	/* check devices */
	rc = sqlite3_exec (priv->db, "SELECT * FROM pending LIMIT 1",
			   NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("FuPending: creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE pending ("
			    "device_id TEXT PRIMARY KEY,"
			    "state INTEGER DEFAULT 0,"
			    "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL,"
			    "error TEXT,"
			    "filename TEXT,"
			    "display_name TEXT,"
			    "provider TEXT,"
			    "version_old TEXT,"
			    "version_new TEXT);";
		rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
		if (rc != SQLITE_OK) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "Cannot create database: %s",
				     error_msg);
			sqlite3_free (error_msg);
			return FALSE;
		}
	}

	/* check pending has state and provider (since 0.1.1) */
	rc = sqlite3_exec (priv->db,
			   "SELECT provider FROM pending LIMIT 1",
			   NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("FuPending: altering table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "ALTER TABLE pending ADD COLUMN state INTEGER DEFAULT 0;";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		statement = "ALTER TABLE pending ADD COLUMN error TEXT;";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		statement = "ALTER TABLE pending ADD COLUMN provider TEXT;";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	}

	/* check pending has timestamp (since 0.6.2) */
	rc = sqlite3_exec (priv->db,
			   "SELECT timestamp FROM pending LIMIT 1",
			   NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("FuPending: altering table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "ALTER TABLE pending ADD COLUMN timestamp TIMESTAMP "
			    "DEFAULT CURRENT_TIMESTAMP NOT NULL;";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	}

	return TRUE;
}

gboolean
fu_pending_add_device (FuPending *pending, FuDevice *device, GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	char *error_msg = NULL;
	char *statement;
	gboolean ret = TRUE;
	gint rc;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);

	/* lazy load */
	if (priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	g_debug ("FuPending: add device %s", fu_device_get_id (device));
	statement = sqlite3_mprintf ("INSERT INTO pending (device_id,"
							  "state,"
							  "filename,"
							  "display_name,"
							  "provider,"
							  "version_old,"
							  "version_new) "
				     "VALUES ('%q','%i','%q','%q','%q','%q','%q')",
				     fu_device_get_id (device),
				     FWUPD_UPDATE_STATE_PENDING,
				     fu_device_get_filename_pending (device),
				     fu_device_get_name (device),
				     fu_device_get_plugin (device),
				     fu_device_get_version (device),
				     fu_device_get_version_new (device));

	/* insert entry */
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
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

gboolean
fu_pending_remove_all (FuPending *pending, GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	char *error_msg = NULL;
	gint rc;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);

	/* lazy load */
	if (priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	/* remove entries */
	g_debug ("FuPending: removing all devices");
	rc = sqlite3_exec (priv->db, "DELETE FROM pending;", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_pending_remove_device (FuPending *pending, FuDevice *device, GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	char *error_msg = NULL;
	char *statement;
	gboolean ret = TRUE;
	gint rc;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);

	/* lazy load */
	if (priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	g_debug ("FuPending: remove device %s", fu_device_get_id (device));
	statement = sqlite3_mprintf ("DELETE FROM pending WHERE "
				     "device_id = '%q';",
				     fu_device_get_id (device));

	/* remove entry */
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
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

static gint
fu_pending_device_sqlite_cb (void *data,
			gint argc,
			gchar **argv,
			gchar **col_name)
{
	GPtrArray *array = (GPtrArray *) data;
	FuDevice *device;

	/* create new result */
	device = fu_device_new ();
	g_ptr_array_add (array, device);

	g_debug ("FuPending: got sql result %s", argv[0]);
	for (gint i = 0; i < argc; i++) {
		if (g_strcmp0 (col_name[i], "device_id") == 0) {
			fu_device_set_id (device, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "filename") == 0) {
			fu_device_set_filename_pending (device, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "display_name") == 0) {
			fu_device_set_name (device, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "version_old") == 0) {
			fu_device_set_version (device, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "version_new") == 0) {
			fu_device_set_version_new (device, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "provider") == 0) {
			fu_device_set_plugin (device, argv[i]);
			continue;
		}
		if (g_strcmp0 (col_name[i], "state") == 0) {
			FwupdUpdateState state = atoi (argv[i]);
			fu_device_set_update_state (device, state);
			continue;
		}
		if (g_strcmp0 (col_name[i], "timestamp") == 0) {
			guint64 timestamp = g_ascii_strtoull (argv[i], NULL, 10);
			if (timestamp > 0)
				fu_device_set_created (device, timestamp);
			continue;
		}
		if (g_strcmp0 (col_name[i], "error") == 0) {
			if (argv[i] != NULL)
				fu_device_set_update_error (device, argv[i]);
			continue;
		}
	}

	return 0;
}

FuDevice *
fu_pending_get_device (FuPending *pending, const gchar *device_id, GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	FuDevice *device = NULL;
	char *error_msg = NULL;
	char *statement;
	gint rc;
	g_autoptr(GPtrArray) array_tmp = NULL;

	g_return_val_if_fail (FU_IS_PENDING (pending), NULL);

	/* lazy load */
	if (priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return NULL;
	}

	/* get all the devices */
	g_debug ("FuPending: get device");
	statement = sqlite3_mprintf ("SELECT * FROM pending WHERE "
				     "device_id = '%q';",
				     device_id);
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	rc = sqlite3_exec (priv->db,
			   statement,
			   fu_pending_device_sqlite_cb,
			   array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "SQL error: %s",
			     error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
	if (array_tmp->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No devices found");
		goto out;
	}
	device = g_object_ref (g_ptr_array_index (array_tmp, 0));
out:
	sqlite3_free (statement);
	return device;
}

GPtrArray *
fu_pending_get_devices (FuPending *pending, GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	GPtrArray *array = NULL;
	char *error_msg = NULL;
	char *statement;
	gint rc;
	g_autoptr(GPtrArray) array_tmp = NULL;

	g_return_val_if_fail (FU_IS_PENDING (pending), NULL);

	/* lazy load */
	if (priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return NULL;
	}

	/* get all the devices */
	g_debug ("FuPending: get devices");
	statement = sqlite3_mprintf ("SELECT * FROM pending;");
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	rc = sqlite3_exec (priv->db,
			   statement,
			   fu_pending_device_sqlite_cb,
			   array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
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

gboolean
fu_pending_set_state (FuPending *pending,
		      FuDevice *device,
		      FwupdUpdateState state,
		      GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	char *error_msg = NULL;
	char *statement;
	gboolean ret = TRUE;
	gint rc;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);

	/* lazy load */
	if (priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	g_debug ("FuPending: set state of %s to %s",
		 fu_device_get_id (device),
		 fwupd_update_state_to_string (state));
	statement = sqlite3_mprintf ("UPDATE pending SET state='%i' WHERE "
				     "device_id = '%q';",
				     state, fu_device_get_id (device));

	/* remove entry */
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
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

gboolean
fu_pending_set_error_msg (FuPending *pending,
			  FuDevice *device,
			  const gchar *error_msg2,
			  GError **error)
{
	FuPendingPrivate *priv = GET_PRIVATE (pending);
	char *error_msg = NULL;
	char *statement;
	gboolean ret = TRUE;
	gint rc;

	g_return_val_if_fail (FU_IS_PENDING (pending), FALSE);

	/* lazy load */
	if (priv->db == NULL) {
		if (!fu_pending_load (pending, error))
			return FALSE;
	}

	g_debug ("FuPending: add comment to %s: %s",
		 fu_device_get_id (device), error_msg2);
	statement = sqlite3_mprintf ("UPDATE pending SET error='%q' WHERE "
				     "device_id = '%q';",
				     error_msg2,
				     fu_device_get_id (device));

	/* remove entry */
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
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

static void
fu_pending_class_init (FuPendingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_pending_finalize;
}

static void
fu_pending_init (FuPending *pending)
{
}

static void
fu_pending_finalize (GObject *object)
{
	FuPending *pending = FU_PENDING (object);
	FuPendingPrivate *priv = GET_PRIVATE (pending);

	if (priv->db != NULL)
		sqlite3_close (priv->db);

	G_OBJECT_CLASS (fu_pending_parent_class)->finalize (object);
}

FuPending *
fu_pending_new (void)
{
	FuPending *pending;
	pending = g_object_new (FU_TYPE_PENDING, NULL);
	return FU_PENDING (pending);
}
