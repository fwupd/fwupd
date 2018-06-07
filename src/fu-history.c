/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <sqlite3.h>
#include <stdlib.h>

#include "fu-common.h"
#include "fu-device-private.h"
#include "fu-history.h"

static void fu_history_finalize			 (GObject *object);

struct _FuHistory
{
	GObject			 parent_instance;
	sqlite3			*db;
};

G_DEFINE_TYPE (FuHistory, fu_history, G_TYPE_OBJECT)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(sqlite3_stmt, sqlite3_finalize);
#pragma clang diagnostic pop

static FuDevice *
fu_history_device_from_stmt (sqlite3_stmt *stmt)
{
	const gchar *tmp;
	FuDevice *device;
	FwupdRelease *release;

	/* create new result */
	device = fu_device_new ();
	release = fu_device_get_release_default (device);

	/* device_id */
	tmp = (const gchar *) sqlite3_column_text (stmt, 0);
	g_debug ("FuHistory: got sql result %s", tmp);
	if (tmp != NULL)
		fwupd_device_set_id (FWUPD_DEVICE (device), tmp);

	/* checksum */
	tmp = (const gchar *) sqlite3_column_text (stmt, 1);
	if (tmp != NULL)
		fwupd_release_add_checksum (release, tmp);

	/* plugin */
	tmp = (const gchar *) sqlite3_column_text (stmt, 2);
	if (tmp != NULL)
		fu_device_set_plugin (device, tmp);

	/* device_created */
	fu_device_set_created (device, sqlite3_column_int64 (stmt, 3));

	/* device_modified */
	fu_device_set_modified (device, sqlite3_column_int64 (stmt, 4));

	/* display_name */
	tmp = (const gchar *) sqlite3_column_text (stmt, 5);
	if (tmp != NULL)
		fu_device_set_name (device, tmp);

	/* filename */
	tmp = (const gchar *) sqlite3_column_text (stmt, 6);
	if (tmp != NULL)
		fwupd_release_set_filename (release, tmp);

	/* flags */
	fu_device_set_flags (device, sqlite3_column_int64 (stmt, 7));

	/* metadata */
	tmp = (const gchar *) sqlite3_column_text (stmt, 8);
	if (tmp != NULL) {
		g_auto(GStrv) split = g_strsplit (tmp, ";", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_auto(GStrv) kv = g_strsplit (split[i], "=", 2);
			if (g_strv_length (kv) != 2)
				continue;
			fwupd_release_add_metadata_item (release, kv[0], kv[1]);
		}
	}

	/* guid_default */
	tmp = (const gchar *) sqlite3_column_text (stmt, 9);
	if (tmp != NULL)
		fu_device_add_guid (device, tmp);

	/* update_state */
	fu_device_set_update_state (device, sqlite3_column_int (stmt, 10));

	/* update_error */
	tmp = (const gchar *) sqlite3_column_text (stmt, 11);
	fu_device_set_update_error (device, tmp);

	/* version_new */
	tmp = (const gchar *) sqlite3_column_text (stmt, 12);
	if (tmp != NULL)
		fwupd_release_set_version (release, tmp);

	/* version_old */
	tmp = (const gchar *) sqlite3_column_text (stmt, 13);
	if (tmp != NULL)
		fu_device_set_version (device, tmp);
	return device;
}

static gboolean
fu_history_stmt_exec (FuHistory *self, sqlite3_stmt *stmt,
		      GPtrArray *array, GError **error)
{
	gint rc;
	if (array == NULL) {
		rc = sqlite3_step (stmt);
	} else {
		while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
			FuDevice *device = fu_history_device_from_stmt (stmt);
			g_ptr_array_add (array, device);
		}
	}
	if (rc != SQLITE_DONE) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_WRITE,
			     "failed to execute prepared statement: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_create_database (FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec (self->db,
			 "BEGIN TRANSACTION;"
			 "CREATE TABLE schema ("
			 "created timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
			 "version INTEGER DEFAULT 0);"
			 "INSERT INTO schema (version) VALUES (2);"
			 "CREATE TABLE history ("
			 "device_id TEXT,"
			 "update_state INTEGER DEFAULT 0,"
			 "update_error TEXT,"
			 "filename TEXT,"
			 "display_name TEXT,"
			 "plugin TEXT,"
			 "device_created INTEGER DEFAULT 0,"
			 "device_modified INTEGER DEFAULT 0,"
			 "checksum TEXT DEFAULT NULL,"
			 "flags INTEGER DEFAULT 0,"
			 "metadata TEXT DEFAULT NULL,"
			 "guid_default TEXT DEFAULT NULL,"
			 "version_old TEXT,"
			 "version_new TEXT);"
			 "COMMIT;", NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to create database: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v1 (FuHistory *self, GError **error)
{
	gint rc;

	/* rename the table to something out the way */
	rc = sqlite3_exec (self->db,
			   "ALTER TABLE history RENAME TO history_old;",
			   NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_debug ("FuHistory: cannot rename v0 table: %s", sqlite3_errmsg (self->db));
		return TRUE;
	}

	/* create new table */
	if (!fu_history_create_database (self, error))
		return FALSE;

	/* migrate the old entries to the new table */
	rc = sqlite3_exec (self->db,
			   "INSERT INTO history SELECT * FROM history_old;"
			   "DROP TABLE history_old;",
			   NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_debug ("FuHistory: no history to migrate: %s", sqlite3_errmsg (self->db));
		return TRUE;
	}
	return TRUE;
}

/* returns 0 if database is not initialised */
static guint
fu_history_get_schema_version (FuHistory *self)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	rc = sqlite3_prepare_v2 (self->db,
				 "SELECT version FROM schema LIMIT 1;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_debug ("no schema version: %s", sqlite3_errmsg (self->db));
		return 0;
	}
	rc = sqlite3_step (stmt);
	if (rc != SQLITE_ROW) {
		g_warning ("failed to execute prepared statement: %s",
			   sqlite3_errmsg (self->db));
		return 0;
	}
	return sqlite3_column_int (stmt, 0);
}

static gboolean
fu_history_load (FuHistory *self, GError **error)
{
	gint rc;
	guint schema_ver;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(GFile) file = NULL;

	/* already done */
	if (self->db != NULL)
		return TRUE;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (self->db == NULL, FALSE);

	/* create directory */
	dirname = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	file = g_file_new_for_path (dirname);
	if (!g_file_query_exists (file, NULL)) {
		if (!g_file_make_directory_with_parents (file, NULL, error))
			return FALSE;
	}

	/* open */
	filename = g_build_filename (dirname, "pending.db", NULL);
	g_debug ("FuHistory: trying to open database '%s'", filename);
	rc = sqlite3_open (filename, &self->db);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "Can't open %s: %s",
			     filename, sqlite3_errmsg (self->db));
		sqlite3_close (self->db);
		return FALSE;
	}

	/* check database */
	schema_ver = fu_history_get_schema_version (self);
	if (schema_ver == 0) {
		g_autoptr(sqlite3_stmt) stmt = NULL;
		rc = sqlite3_prepare_v2 (self->db,
					 "SELECT * FROM history LIMIT 0;",
					 -1, &stmt, NULL);
		if (rc == SQLITE_OK)
			schema_ver = 1;
	}
	g_debug ("FuHistory: got schema version of %u", schema_ver);

	/* migrate schema */
	if (schema_ver == 0) {
		g_debug ("FuHistory: building initial database");
		if (!fu_history_create_database (self, error))
			return FALSE;
	} else if (schema_ver == 1) {
		g_debug ("FuHistory: migrating v%u database", schema_ver);
		if (!fu_history_migrate_database_v1 (self, error))
			return FALSE;
	}

	return TRUE;
}

static gchar *
_convert_hash_to_string (GHashTable *hash)
{
	GString *str = g_string_new (NULL);
	g_autoptr(GList) keys = g_hash_table_get_keys (hash);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (hash, key);
		if (str->len > 0)
			g_string_append (str, ";");
		g_string_append_printf (str, "%s=%s", key, value);
	}
	return g_string_free (str, FALSE);
}

/* unset some flags we don't want to store */
static FwupdDeviceFlags
fu_history_get_device_flags_filtered (FuDevice *device)
{
	FwupdDeviceFlags flags = fu_device_get_flags (device);
	flags &= ~FWUPD_DEVICE_FLAG_REGISTERED;
	flags &= ~FWUPD_DEVICE_FLAG_SUPPORTED;
	return flags;
}

gboolean
fu_history_modify_device (FuHistory *self, FuDevice *device,
			  FuHistoryFlags flags,
			  GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* overwrite entry if it exists */
	if ((flags & FU_HISTORY_FLAGS_MATCH_OLD_VERSION) &&
	    (flags & FU_HISTORY_FLAGS_MATCH_NEW_VERSION)) {
		g_debug ("FuHistory: modifying device %s [%s], version not important",
			 fu_device_get_name (device),
			 fu_device_get_id (device));
		rc = sqlite3_prepare_v2 (self->db,
					 "UPDATE history SET "
					 "update_state = ?1, "
					 "update_error = ?2, "
					 "flags = ?3 "
					 "WHERE device_id = ?4;",
					 -1, &stmt, NULL);
	} else if (flags & FU_HISTORY_FLAGS_MATCH_OLD_VERSION) {
		g_debug ("FuHistory: modifying device %s [%s], only version old %s",
			 fu_device_get_name (device),
			 fu_device_get_id (device),
			 fu_device_get_version (device));
		rc = sqlite3_prepare_v2 (self->db,
					 "UPDATE history SET "
					 "update_state = ?1, "
					 "update_error = ?2, "
					 "flags = ?3 "
					 "WHERE device_id = ?4 AND version_old = ?5;",
					 -1, &stmt, NULL);
	} else if (flags & FU_HISTORY_FLAGS_MATCH_NEW_VERSION) {
		g_debug ("FuHistory: modifying device %s [%s], only version new %s",
			 fu_device_get_name (device),
			 fu_device_get_id (device),
			 fu_device_get_version (device));
		rc = sqlite3_prepare_v2 (self->db,
					 "UPDATE history SET "
					 "update_state = ?1, "
					 "update_error = ?2, "
					 "flags = ?3 "
					 "WHERE device_id = ?4 AND version_new = ?5;",
					 -1, &stmt, NULL);
	} else {
		g_assert_not_reached ();
	}
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_int (stmt, 1, fu_device_get_update_state (device));
	sqlite3_bind_text (stmt, 2, fu_device_get_update_error (device), -1, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 3, fu_history_get_device_flags_filtered (device));
	sqlite3_bind_text (stmt, 4, fu_device_get_id (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 5, fu_device_get_version (device), -1, SQLITE_STATIC);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

gboolean
fu_history_add_device (FuHistory *self, FuDevice *device, FwupdRelease *release, GError **error)
{
	const gchar *checksum = NULL;
	gint rc;
	g_autofree gchar *metadata = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* ensure device with this old-version -> new-version does not exist */
	if (!fu_history_remove_device (self, device, release, error))
		return FALSE;

	g_debug ("FuHistory: add device %s [%s]",
		 fu_device_get_name (device),
		 fu_device_get_id (device));
	if (release != NULL) {
		GPtrArray *checksums = fwupd_release_get_checksums (release);
		checksum = fwupd_checksum_get_by_kind (checksums, G_CHECKSUM_SHA1);
	}

	/* metadata is stored as a simple string */
	metadata = _convert_hash_to_string (fwupd_release_get_metadata (release));

	/* add */
	rc = sqlite3_prepare_v2 (self->db,
				 "INSERT INTO history (device_id,"
						      "update_state,"
						      "update_error,"
						      "flags,"
						      "filename,"
						      "checksum,"
						      "display_name,"
						      "plugin,"
						      "guid_default,"
						      "metadata,"
						      "device_created,"
						      "device_modified,"
						      "version_old,"
						      "version_new) "
				 "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,"
					 "?11,?12,?13,?14)", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_text (stmt, 1, fu_device_get_id (device), -1, SQLITE_STATIC);
	sqlite3_bind_int (stmt, 2, fu_device_get_update_state (device));
	sqlite3_bind_text (stmt, 3, fu_device_get_update_error (device), -1, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 4, fu_history_get_device_flags_filtered (device));
	sqlite3_bind_text (stmt, 5, fwupd_release_get_filename (release), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 6, checksum, -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 7, fu_device_get_name (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 8, fu_device_get_plugin (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 9, fu_device_get_guid_default (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 10, metadata, -1, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 11, fu_device_get_created (device));
	sqlite3_bind_int64 (stmt, 12, fu_device_get_modified (device));
	sqlite3_bind_text (stmt, 13, fu_device_get_version (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 14, fwupd_release_get_version (release), -1, SQLITE_STATIC);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

gboolean
fu_history_remove_all_with_state (FuHistory *self,
				  FwupdUpdateState update_state,
				  GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* remove entries */
	g_debug ("FuHistory: removing all devices with update_state %s",
		 fwupd_update_state_to_string (update_state));
	rc = sqlite3_prepare_v2 (self->db,
				 "DELETE FROM history WHERE update_state = ?1",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_int (stmt, 1, update_state);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

gboolean
fu_history_remove_all (FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* remove entries */
	g_debug ("FuHistory: removing all devices");
	rc = sqlite3_prepare_v2 (self->db, "DELETE FROM history;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

gboolean
fu_history_remove_device (FuHistory *self,  FuDevice *device,
			  FwupdRelease *release, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	g_debug ("FuHistory: remove device %s [%s]",
		 fu_device_get_name (device),
		 fu_device_get_id (device));
	rc = sqlite3_prepare_v2 (self->db,
				 "DELETE FROM history WHERE device_id = ?1 "
				 "AND version_old = ?2 "
				 "AND version_new = ?3;",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_text (stmt, 1, fu_device_get_id (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 2, fu_device_get_version (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 3, fwupd_release_get_version (release), -1, SQLITE_STATIC);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

FuDevice *
fu_history_get_device_by_id (FuHistory *self, const gchar *device_id, GError **error)
{
	gint rc;
	g_autoptr(GPtrArray) array_tmp = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);

	/* lazy load */
	if (!fu_history_load (self, error))
		return NULL;

	/* get all the devices */
	g_debug ("FuHistory: get device");
	rc = sqlite3_prepare_v2 (self->db,
				 "SELECT device_id, "
					"checksum, "
					"plugin, "
					"device_created, "
					"device_modified, "
					"display_name, "
					"filename, "
					"flags, "
					"metadata, "
					"guid_default, "
					"update_state, "
					"update_error, "
					"version_new, "
					"version_old FROM history WHERE "
				 "device_id = ?1 LIMIT 1", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL: %s",
			     sqlite3_errmsg (self->db));
		return NULL;
	}
	sqlite3_bind_text (stmt, 1, device_id, -1, SQLITE_STATIC);
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	if (!fu_history_stmt_exec (self, stmt, array_tmp, error))
		return NULL;
	if (array_tmp->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No devices found");
		return NULL;
	}
	return g_object_ref (g_ptr_array_index (array_tmp, 0));
}

GPtrArray *
fu_history_get_devices (FuHistory *self, GError **error)
{
	GPtrArray *array = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	gint rc;
	g_autoptr(GPtrArray) array_tmp = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), NULL);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load (self, error))
			return NULL;
	}

	/* get all the devices */
	g_debug ("FuHistory: get devices");
	rc = sqlite3_prepare_v2 (self->db,
				 "SELECT device_id, "
					"checksum, "
					"plugin, "
					"device_created, "
					"device_modified, "
					"display_name, "
					"filename, "
					"flags, "
					"metadata, "
					"guid_default, "
					"update_state, "
					"update_error, "
					"version_new, "
					"version_old FROM history "
					"ORDER BY device_modified ASC;",
					-1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL: %s",
			     sqlite3_errmsg (self->db));
		return NULL;
	}
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	if (!fu_history_stmt_exec (self, stmt, array_tmp, error))
		return NULL;
	array = g_ptr_array_ref (array_tmp);
	return array;
}

static void
fu_history_class_init (FuHistoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_history_finalize;
}

static void
fu_history_init (FuHistory *self)
{
}

static void
fu_history_finalize (GObject *object)
{
	FuHistory *self = FU_HISTORY (object);

	if (self->db != NULL)
		sqlite3_close (self->db);

	G_OBJECT_CLASS (fu_history_parent_class)->finalize (object);
}

FuHistory *
fu_history_new (void)
{
	FuHistory *self;
	self = g_object_new (FU_TYPE_PENDING, NULL);
	return FU_HISTORY (self);
}
