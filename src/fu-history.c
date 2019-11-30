/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuHistory"

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <sqlite3.h>
#include <stdlib.h>

#include "fu-common.h"
#include "fu-device-private.h"
#include "fu-history.h"
#include "fu-mutex.h"

#define FU_HISTORY_CURRENT_SCHEMA_VERSION	5

static void fu_history_finalize			 (GObject *object);

struct _FuHistory
{
	GObject			 parent_instance;
	sqlite3			*db;
	GRWLock			 db_mutex;
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
	fu_device_set_flags (device, sqlite3_column_int64 (stmt, 7) |
				     FWUPD_DEVICE_FLAG_HISTORICAL);

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
		fu_device_set_version (device, tmp, FWUPD_VERSION_FORMAT_UNKNOWN);

	/* checksum_device */
	tmp = (const gchar *) sqlite3_column_text (stmt, 14);
	if (tmp != NULL)
		fu_device_add_checksum (device, tmp);

	/* protocol */
	tmp = (const gchar *) sqlite3_column_text (stmt, 15);
	if (tmp != NULL)
		fwupd_release_set_protocol (release, tmp);
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
			 "CREATE TABLE IF NOT EXISTS schema ("
			 "created timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
			 "version INTEGER DEFAULT 0);"
			 "INSERT INTO schema (version) VALUES (0);"
			 "CREATE TABLE IF NOT EXISTS history ("
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
			 "version_new TEXT,"
			 "checksum_device TEXT DEFAULT NULL,"
			 "protocol TEXT DEFAULT NULL);"
			 "CREATE TABLE IF NOT EXISTS approved_firmware ("
			 "checksum TEXT);"
			 "COMMIT;", NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL for creating tables: %s",
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
		g_debug ("cannot rename v0 table: %s", sqlite3_errmsg (self->db));
		return TRUE;
	}

	/* create new table */
	if (!fu_history_create_database (self, error))
		return FALSE;

	/* migrate the old entries to the new table */
	rc = sqlite3_exec (self->db,
			   "INSERT INTO history SELECT "
			   "device_id, update_state, update_error, filename, "
			   "display_name, plugin, device_created, device_modified, "
			   "checksum, flags, metadata, guid_default, version_old, "
			   "version_new, NULL, NULL FROM history_old;"
			   "DROP TABLE history_old;",
			   NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_debug ("no history to migrate: %s", sqlite3_errmsg (self->db));
		return TRUE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v2 (FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec (self->db,
			   "ALTER TABLE history ADD COLUMN checksum_device TEXT DEFAULT NULL;",
			   NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to alter database: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v3 (FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec (self->db,
			   "ALTER TABLE history ADD COLUMN protocol TEXT DEFAULT NULL;",
			   NULL, NULL, NULL);
	if (rc != SQLITE_OK)
		g_debug ("ignoring database error: %s", sqlite3_errmsg (self->db));
	return TRUE;
}

static gboolean
fu_history_migrate_database_v4 (FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec (self->db,
			   "CREATE TABLE IF NOT EXISTS approved_firmware (checksum TEXT);",
			   NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to create table: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
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
				 "SELECT version FROM schema LIMIT 1;",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_debug ("no schema version: %s", sqlite3_errmsg (self->db));
		return 0;
	}
	rc = sqlite3_step (stmt);
	if (rc != SQLITE_ROW) {
		g_warning ("failed prepare to get schema version: %s",
			   sqlite3_errmsg (self->db));
		return 0;
	}
	return sqlite3_column_int (stmt, 0);
}

static gboolean
fu_history_create_or_migrate (FuHistory *self, guint schema_ver, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	/* create initial up-to-date database or migrate */
	if (schema_ver == 0) {
		g_debug ("building initial database");
		if (!fu_history_create_database (self, error))
			return FALSE;
	} else if (schema_ver == 1) {
		g_debug ("migrating v%u database by recreating table", schema_ver);
		if (!fu_history_migrate_database_v1 (self, error))
			return FALSE;
	} else if (schema_ver == 2) {
		g_debug ("migrating v%u database by altering", schema_ver);
		if (!fu_history_migrate_database_v2 (self, error))
			return FALSE;
		if (!fu_history_migrate_database_v3 (self, error))
			return FALSE;
		if (!fu_history_migrate_database_v4 (self, error))
			return FALSE;
	} else if (schema_ver == 3) {
		g_debug ("migrating v%u database by altering", schema_ver);
		if (!fu_history_migrate_database_v3 (self, error))
			return FALSE;
		if (!fu_history_migrate_database_v4 (self, error))
			return FALSE;
	} else if (schema_ver == 4) {
		g_debug ("migrating v%u database by altering", schema_ver);
		if (!fu_history_migrate_database_v4 (self, error))
			return FALSE;
	} else {
		/* this is probably okay, but return an error if we ever delete
		 * or rename columns */
		g_warning ("schema version %u is unknown", schema_ver);
		return TRUE;
	}

	/* set new schema version */
	rc = sqlite3_prepare_v2 (self->db,
				 "UPDATE schema SET version=?1;",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL for updating schema: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_int (stmt, 1, FU_HISTORY_CURRENT_SCHEMA_VERSION);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

static gboolean
fu_history_open (FuHistory *self, const gchar *filename, GError **error)
{
	gint rc;
	g_debug ("trying to open database '%s'", filename);
	rc = sqlite3_open (filename, &self->db);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "Can't open %s: %s",
			     filename, sqlite3_errmsg (self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_load (FuHistory *self, GError **error)
{
	gint rc;
	guint schema_ver;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GRWLockWriterLocker) locker = g_rw_lock_writer_locker_new (&self->db_mutex);

	/* already done */
	if (self->db != NULL)
		return TRUE;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (self->db == NULL, FALSE);
	g_return_val_if_fail (locker != NULL, FALSE);

	/* create directory */
	dirname = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	file = g_file_new_for_path (dirname);
	if (!g_file_query_exists (file, NULL)) {
		if (!g_file_make_directory_with_parents (file, NULL, error))
			return FALSE;
	}

	/* open */
	filename = g_build_filename (dirname, "pending.db", NULL);
	if (!fu_history_open (self, filename, error))
		return FALSE;

	/* check database */
	schema_ver = fu_history_get_schema_version (self);
	if (schema_ver == 0) {
		g_autoptr(sqlite3_stmt) stmt_tmp = NULL;
		rc = sqlite3_prepare_v2 (self->db,
					 "SELECT * FROM history LIMIT 0;",
					 -1, &stmt_tmp, NULL);
		if (rc == SQLITE_OK)
			schema_ver = 1;
	}

	/* create initial up-to-date database, or migrate */
	g_debug ("got schema version of %u", schema_ver);
	if (schema_ver != FU_HISTORY_CURRENT_SCHEMA_VERSION) {
		g_autoptr(GError) error_migrate = NULL;
		if (!fu_history_create_or_migrate (self, schema_ver, &error_migrate)) {
			/* this is fatal to the daemon, so delete the database
			 * and try again with something empty */
			g_warning ("failed to migrate %s database: %s",
				   filename, error_migrate->message);
			sqlite3_close (self->db);
			if (g_unlink (filename) != 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Can't delete %s", filename);
				return FALSE;
			}
			if (!fu_history_open (self, filename, error))
				return FALSE;
			return fu_history_create_database (self, error);
		}
	}

	/* success */
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

/**
 * fu_history_modify_device:
 * @self: A #FuHistory
 * @device: A #FuDevice
 * @error: A #GError or NULL
 *
 * Modify a device in the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_modify_device (FuHistory *self, FuDevice *device, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* overwrite entry if it exists */
	locker = g_rw_lock_writer_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
	g_debug ("modifying device %s [%s]",
		 fu_device_get_name (device),
		 fu_device_get_id (device));
	rc = sqlite3_prepare_v2 (self->db,
				 "UPDATE history SET "
				 "update_state = ?1, "
				 "update_error = ?2, "
				 "checksum_device = ?6, "
				 "device_modified = ?7, "
				 "flags = ?3 "
				 "WHERE device_id = ?4;",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to update history: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}

	sqlite3_bind_int (stmt, 1, fu_device_get_update_state (device));
	sqlite3_bind_text (stmt, 2, fu_device_get_update_error (device), -1, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 3, fu_history_get_device_flags_filtered (device));
	sqlite3_bind_text (stmt, 4, fu_device_get_id (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 5, fu_device_get_version (device), -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 6, fwupd_checksum_get_by_kind (fu_device_get_checksums (device),
								G_CHECKSUM_SHA1), -1, SQLITE_STATIC);
	sqlite3_bind_int64 (stmt, 7, fu_device_get_modified (device));

	return fu_history_stmt_exec (self, stmt, NULL, error);
}

/**
 * fu_history_add_device:
 * @self: A #FuHistory
 * @device: A #FuDevice
 * @release: A #FuRelease
 * @error: A #GError or NULL
 *
 * Adds a device to the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_add_device (FuHistory *self, FuDevice *device, FwupdRelease *release, GError **error)
{
	const gchar *checksum_device;
	const gchar *checksum = NULL;
	gint rc;
	g_autofree gchar *metadata = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* ensure all old device(s) with this ID are removed */
	if (!fu_history_remove_device (self, device, error))
		return FALSE;
	g_debug ("add device %s [%s]",
		 fu_device_get_name (device),
		 fu_device_get_id (device));
	if (release != NULL) {
		GPtrArray *checksums = fwupd_release_get_checksums (release);
		checksum = fwupd_checksum_get_by_kind (checksums, G_CHECKSUM_SHA1);
	}
	checksum_device = fwupd_checksum_get_by_kind (fu_device_get_checksums (device),
						      G_CHECKSUM_SHA1);

	/* metadata is stored as a simple string */
	metadata = _convert_hash_to_string (fwupd_release_get_metadata (release));

	/* add */
	locker = g_rw_lock_writer_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
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
						      "version_new,"
						      "checksum_device,"
						      "protocol) "
				 "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,"
					 "?11,?12,?13,?14,?15,?16)", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to insert history: %s",
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
	sqlite3_bind_text (stmt, 15, checksum_device, -1, SQLITE_STATIC);
	sqlite3_bind_text (stmt, 16, fwupd_release_get_protocol (release), -1, SQLITE_STATIC);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

/**
 * fu_history_remove_all_with_state:
 * @self: A #FuHistory
 * @update_state: A #FwupdUpdateState
 * @error: A #GError or NULL
 *
 * Remove all devices from the history database that match
 * state update_state
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_remove_all_with_state (FuHistory *self,
				  FwupdUpdateState update_state,
				  GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* remove entries */
	locker = g_rw_lock_writer_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
	g_debug ("removing all devices with update_state %s",
		 fwupd_update_state_to_string (update_state));
	rc = sqlite3_prepare_v2 (self->db,
				 "DELETE FROM history WHERE update_state = ?1",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to delete history: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_int (stmt, 1, update_state);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

/**
 * fu_history_remove_all:
 * @self: A #FuHistory
 * @error: A #GError or NULL
 *
 * Remove all devices from the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_remove_all (FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* remove entries */
	locker = g_rw_lock_writer_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
	g_debug ("removing all devices");
	rc = sqlite3_prepare_v2 (self->db, "DELETE FROM history;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to delete history: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

/**
 * fu_history_remove_device:
 * @self: A #FuHistory
 * @device: A #FuDevice
 * @error: A #GError or NULL
 *
 * Remove a device from the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_remove_device (FuHistory *self,  FuDevice *device, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	locker = g_rw_lock_writer_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
	g_debug ("remove device %s [%s]",
		 fu_device_get_name (device),
		 fu_device_get_id (device));
	rc = sqlite3_prepare_v2 (self->db,
				 "DELETE FROM history WHERE device_id = ?1;",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to delete history: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_text (stmt, 1, fu_device_get_id (device), -1, SQLITE_STATIC);
	return fu_history_stmt_exec (self, stmt, NULL, error);
}


/**
 * fu_history_get_device_by_id:
 * @self: A #FuHistory
 * @device_id: A string
 * @error: A #GError or NULL
 *
 * Returns the device from the history database or NULL if not found
 *
 * Returns: (transfer full): a #FuDevice
 *
 * Since: 1.0.4
 **/
FuDevice *
fu_history_get_device_by_id (FuHistory *self, const gchar *device_id, GError **error)
{
	gint rc;
	g_autoptr(GPtrArray) array_tmp = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);

	/* lazy load */
	if (!fu_history_load (self, error))
		return NULL;

	/* get all the devices */
	locker = g_rw_lock_reader_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, NULL);
	g_debug ("get device");
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
					"version_old, "
					"checksum_device, "
					"protocol FROM history WHERE "
				 "device_id = ?1 ORDER BY device_created DESC "
				 "LIMIT 1", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to get history: %s",
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
/**
 * fu_history_get_devices:
 * @self: A #FuHistory
 * @error: A #GError or NULL
 *
 * Gets the devices in the history database.
 *
 * Returns: (element-type #FuDevice) (transfer container): devices
 *
 * Since: 1.0.4
 **/
GPtrArray *
fu_history_get_devices (FuHistory *self, GError **error)
{
	GPtrArray *array = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	gint rc;
	g_autoptr(GPtrArray) array_tmp = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), NULL);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load (self, error))
			return NULL;
	}

	/* get all the devices */
	locker = g_rw_lock_reader_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, NULL);
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
					"version_old, "
					"checksum_device, "
					"protocol FROM history "
					"ORDER BY device_modified ASC;",
					-1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to get history: %s",
			     sqlite3_errmsg (self->db));
		return NULL;
	}
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	if (!fu_history_stmt_exec (self, stmt, array_tmp, error))
		return NULL;
	array = g_ptr_array_ref (array_tmp);
	return array;
}

/**
 * fu_history_get_approved_firmware:
 * @self: A #FuHistory
 * @error: A #GError or NULL
 *
 * Returns approved firmware records.
 *
 * Returns: (transfer full) (element-type gchar *): records
 *
 * Since: 1.2.6
 **/
GPtrArray *
fu_history_get_approved_firmware (FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), NULL);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load (self, error))
			return NULL;
	}

	/* get all the approved firmware */
	locker = g_rw_lock_reader_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, NULL);
	rc = sqlite3_prepare_v2 (self->db,
				 "SELECT checksum FROM approved_firmware;",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to get checksum: %s",
			     sqlite3_errmsg (self->db));
		return NULL;
	}
	array = g_ptr_array_new_with_free_func (g_free);
	while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
		const gchar *tmp = (const gchar *) sqlite3_column_text (stmt, 0);
		g_ptr_array_add (array, g_strdup (tmp));
	}
	if (rc != SQLITE_DONE) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_WRITE,
			     "failed to execute prepared statement: %s",
			     sqlite3_errmsg (self->db));
		return NULL;
	}
	return g_steal_pointer (&array);
}

/**
 * fu_history_clear_approved_firmware:
 * @self: A #FuHistory
 * @error: A #GError or NULL
 *
 * Clear all approved firmware records
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.2.6
 **/
gboolean
fu_history_clear_approved_firmware (FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* remove entries */
	locker = g_rw_lock_writer_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
	rc = sqlite3_prepare_v2 (self->db,
				 "DELETE FROM approved_firmware;",
				 -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to delete approved firmware: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	return fu_history_stmt_exec (self, stmt, NULL, error);
}

/**
 * fu_history_add_approved_firmware:
 * @self: A #FuHistory
 * @checksum: a string
 * @error: A #GError or NULL
 *
 * Add an approved firmware record to the database
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.2.6
 **/
gboolean
fu_history_add_approved_firmware (FuHistory *self,
				  const gchar *checksum,
				  GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_HISTORY (self), FALSE);
	g_return_val_if_fail (checksum != NULL, FALSE);

	/* lazy load */
	if (!fu_history_load (self, error))
		return FALSE;

	/* add */
	locker = g_rw_lock_writer_locker_new (&self->db_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
	rc = sqlite3_prepare_v2 (self->db,
				 "INSERT INTO approved_firmware (checksum) "
				 "VALUES (?1)", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Failed to prepare SQL to insert checksum: %s",
			     sqlite3_errmsg (self->db));
		return FALSE;
	}
	sqlite3_bind_text (stmt, 1, checksum, -1, SQLITE_STATIC);
	return fu_history_stmt_exec (self, stmt, NULL, error);
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
	g_rw_lock_init (&self->db_mutex);
}

static void
fu_history_finalize (GObject *object)
{
	FuHistory *self = FU_HISTORY (object);

	if (self->db != NULL)
		sqlite3_close (self->db);
	g_rw_lock_clear (&self->db_mutex);

	G_OBJECT_CLASS (fu_history_parent_class)->finalize (object);
}

/**
 * fu_history_new:
 *
 * Creates a new #FuHistory
 *
 * Since: 1.0.4
 **/
FuHistory *
fu_history_new (void)
{
	FuHistory *self;
	self = g_object_new (FU_TYPE_PENDING, NULL);
	return FU_HISTORY (self);
}
