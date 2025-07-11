/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuHistory"

#include "config.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>

#include "fu-device-private.h"
#include "fu-history.h"
#include "fu-release.h"
#include "fu-security-attrs-private.h"

/*
 * v1	legacy schema
 * v2	initial schema
 * v3	add checksum_device to history
 * v4	add protocol to history
 * v5	create table approved_firmware
 * v6	create table blocked_firmware
 * v7	create table hsi_history
 * v8	add release_id to history
 * v9	add appstream_id to history
 * v10	add version_format to history
 * v11	no changes, bumped due to bungled migration to v10
 * v12	add install_duration to history
 * v13	add release_flags to history
 * v14	create table emulation_tag
 */
#define FU_HISTORY_CURRENT_SCHEMA_VERSION 14

static void
fu_history_finalize(GObject *object);

struct _FuHistory {
	GObject parent_instance;
	FuContext *ctx;
	sqlite3 *db;
};

G_DEFINE_TYPE(FuHistory, fu_history, G_TYPE_OBJECT)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(sqlite3_stmt, sqlite3_finalize);
#pragma clang diagnostic pop

static FuDevice *
fu_history_device_from_stmt(sqlite3_stmt *stmt)
{
	const gchar *tmp;
	FuDevice *device;
	g_autoptr(FuRelease) release = fu_release_new();

	/* create new result */
	device = fu_device_new(NULL);
	fu_device_add_release(device, FWUPD_RELEASE(release));

	/* device_id */
	tmp = (const gchar *)sqlite3_column_text(stmt, 0);
	if (tmp != NULL)
		fwupd_device_set_id(FWUPD_DEVICE(device), tmp);

	/* checksum */
	tmp = (const gchar *)sqlite3_column_text(stmt, 1);
	if (tmp != NULL)
		fu_release_add_checksum(release, tmp);

	/* plugin */
	tmp = (const gchar *)sqlite3_column_text(stmt, 2);
	if (tmp != NULL)
		fu_device_set_plugin(device, tmp);

	/* device_created */
	fu_device_set_created_usec(device, sqlite3_column_int64(stmt, 3) * G_USEC_PER_SEC);

	/* device_modified */
	fu_device_set_modified_usec(device, sqlite3_column_int64(stmt, 4) * G_USEC_PER_SEC);

	/* display_name */
	tmp = (const gchar *)sqlite3_column_text(stmt, 5);
	if (tmp != NULL)
		fu_device_set_name(device, tmp);

	/* filename */
	tmp = (const gchar *)sqlite3_column_text(stmt, 6);
	if (tmp != NULL)
		fu_release_set_filename(release, tmp);

	/* flags */
	fu_device_set_flags(device, sqlite3_column_int64(stmt, 7) | FWUPD_DEVICE_FLAG_HISTORICAL);

	/* metadata */
	tmp = (const gchar *)sqlite3_column_text(stmt, 8);
	if (tmp != NULL) {
		g_auto(GStrv) split = g_strsplit(tmp, ";", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_auto(GStrv) kv = g_strsplit(split[i], "=", 2);
			if (g_strv_length(kv) != 2)
				continue;
			fu_release_add_metadata_item(release, kv[0], kv[1]);
		}
	}

	/* guid_default */
	tmp = (const gchar *)sqlite3_column_text(stmt, 9);
	if (tmp != NULL)
		fu_device_add_instance_id_full(device, tmp, FU_DEVICE_INSTANCE_FLAG_VISIBLE);

	/* update_state */
	fu_device_set_update_state(device, sqlite3_column_int(stmt, 10));

	/* update_error */
	tmp = (const gchar *)sqlite3_column_text(stmt, 11);
	fu_device_set_update_error(device, tmp);

	/* version_new */
	tmp = (const gchar *)sqlite3_column_text(stmt, 12);
	if (tmp != NULL)
		fu_release_set_version(release, tmp);

	/* version_old */
	tmp = (const gchar *)sqlite3_column_text(stmt, 13);
	if (tmp != NULL)
		fu_device_set_version(device, tmp);

	/* checksum_device */
	tmp = (const gchar *)sqlite3_column_text(stmt, 14);
	if (tmp != NULL)
		fu_device_add_checksum(device, tmp);

	/* protocol */
	tmp = (const gchar *)sqlite3_column_text(stmt, 15);
	if (tmp != NULL)
		fu_release_set_protocol(release, tmp);

	/* release_id */
	tmp = (const gchar *)sqlite3_column_text(stmt, 16);
	if (tmp != NULL)
		fu_release_set_id(release, tmp);

	/* appstream_id */
	tmp = (const gchar *)sqlite3_column_text(stmt, 17);
	if (tmp != NULL)
		fu_release_set_appstream_id(release, tmp);

	/* version_format */
	fu_device_set_version_format(device, sqlite3_column_int(stmt, 18));

	/* install_duration */
	fu_device_set_install_duration(device, sqlite3_column_int(stmt, 19));

	/* release flags */
	fu_release_set_flags(release, sqlite3_column_int(stmt, 20));

	/* success */
	fu_device_convert_instance_ids(device);
	return device;
}

static gboolean
fu_history_stmt_exec(FuHistory *self, sqlite3_stmt *stmt, GPtrArray *array, GError **error)
{
	gint rc;
	if (array == NULL) {
		rc = sqlite3_step(stmt);
	} else {
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			FuDevice *device = fu_history_device_from_stmt(stmt);
			g_ptr_array_add(array, device);
		}
	}
	if (rc != SQLITE_DONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to execute prepared statement: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_create_database(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
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
			  "device_created INTEGER DEFAULT 0,"  /* seconds */
			  "device_modified INTEGER DEFAULT 0," /* seconds */
			  "checksum TEXT DEFAULT NULL,"
			  "flags INTEGER DEFAULT 0,"
			  "metadata TEXT DEFAULT NULL,"
			  "guid_default TEXT DEFAULT NULL,"
			  "version_old TEXT,"
			  "version_new TEXT,"
			  "checksum_device TEXT DEFAULT NULL,"
			  "protocol TEXT DEFAULT NULL,"
			  "release_id TEXT DEFAULT NULL,"
			  "appstream_id TEXT DEFAULT NULL,"
			  "version_format INTEGER DEFAULT 0,"
			  "install_duration INTEGER DEFAULT 0,"
			  "release_flags INTEGER DEFAULT 0);"
			  "CREATE TABLE IF NOT EXISTS approved_firmware ("
			  "checksum TEXT);"
			  "CREATE TABLE IF NOT EXISTS blocked_firmware ("
			  "checksum TEXT);"
			  "CREATE TABLE IF NOT EXISTS hsi_history ("
			  "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
			  "hsi_details TEXT DEFAULT NULL,"
			  "hsi_score TEXT DEFAULT NULL);"
			  "CREATE TABLE emulation_tag (device_id TEXT);"
			  "CREATE UNIQUE INDEX idx_device_id ON emulation_tag (device_id);"
			  "COMMIT;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL for creating tables: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v1(FuHistory *self, GError **error)
{
	gint rc;

	g_info("migrating v1 database by recreating table");
	/* rename the table to something out the way */
	rc = sqlite3_exec(self->db, "ALTER TABLE history RENAME TO history_old;", NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		g_debug("cannot rename v0 table: %s", sqlite3_errmsg(self->db));
		return TRUE;
	}

	/* create new table */
	if (!fu_history_create_database(self, error))
		return FALSE;

	/* migrate the old entries to the new table */
	rc = sqlite3_exec(self->db,
			  "INSERT INTO history SELECT "
			  "device_id, update_state, update_error, filename, "
			  "display_name, plugin, device_created, device_modified, "
			  "checksum, flags, metadata, guid_default, version_old, "
			  "version_new, NULL, NULL, NULL, NULL, NULL, 0, 0 FROM history_old;"
			  "DROP TABLE history_old;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK) {
		g_debug("no history to migrate: %s", sqlite3_errmsg(self->db));
		return TRUE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v2(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "ALTER TABLE history ADD COLUMN checksum_device TEXT DEFAULT NULL;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to alter database: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v3(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "ALTER TABLE history ADD COLUMN protocol TEXT DEFAULT NULL;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK)
		g_debug("ignoring database error: %s", sqlite3_errmsg(self->db));
	return TRUE;
}

static gboolean
fu_history_migrate_database_v4(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "CREATE TABLE IF NOT EXISTS approved_firmware (checksum TEXT);",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create table: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v5(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "CREATE TABLE IF NOT EXISTS blocked_firmware (checksum TEXT);",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create table: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v6(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "CREATE TABLE IF NOT EXISTS hsi_history ("
			  "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
			  "hsi_details TEXT DEFAULT NULL,"
			  "hsi_score TEXT DEFAULT NULL);",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create table: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_history_migrate_database_v7(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "ALTER TABLE history ADD COLUMN release_id TEXT DEFAULT NULL;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK)
		g_debug("ignoring database error: %s", sqlite3_errmsg(self->db));
	return TRUE;
}

static gboolean
fu_history_migrate_database_v8(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "ALTER TABLE history ADD COLUMN appstream_id TEXT DEFAULT NULL;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK)
		g_debug("ignoring database error: %s", sqlite3_errmsg(self->db));
	return TRUE;
}

static gboolean
fu_history_migrate_database_v9(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "ALTER TABLE history ADD COLUMN version_format INTEGER DEFAULT 0;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK)
		g_debug("ignoring database error: %s", sqlite3_errmsg(self->db));
	return TRUE;
}

static gboolean
fu_history_migrate_database_v10(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "ALTER TABLE history ADD COLUMN install_duration INTEGER DEFAULT 0;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK)
		g_debug("ignoring database error: %s", sqlite3_errmsg(self->db));
	return TRUE;
}

static gboolean
fu_history_migrate_database_v11(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(self->db,
			  "ALTER TABLE history ADD COLUMN release_flags INTEGER DEFAULT 0;",
			  NULL,
			  NULL,
			  NULL);
	if (rc != SQLITE_OK)
		g_debug("ignoring database error: %s", sqlite3_errmsg(self->db));
	return TRUE;
}

static gboolean
fu_history_migrate_database_v12(FuHistory *self, GError **error)
{
	gint rc;
	rc = sqlite3_exec(
	    self->db,
	    "BEGIN TRANSACTION;"
	    "CREATE TABLE IF NOT EXISTS emulation_tag (device_id TEXT);"
	    "CREATE UNIQUE INDEX IF NOT EXISTS idx_device_id ON emulation_tag (device_id);"
	    "COMMIT;",
	    NULL,
	    NULL,
	    NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create table: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return TRUE;
}

/* returns 0 if database is not initialized */
static guint
fu_history_get_schema_version(FuHistory *self)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	rc = sqlite3_prepare_v2(self->db, "SELECT version FROM schema LIMIT 1;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_debug("no schema version: %s", sqlite3_errmsg(self->db));
		return 0;
	}
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		g_warning("failed prepare to get schema version: %s", sqlite3_errmsg(self->db));
		return 0;
	}
	return sqlite3_column_int(stmt, 0);
}

static gboolean
fu_history_create_or_migrate(FuHistory *self, guint schema_ver, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	if (schema_ver == 0)
		g_info("building initial database");
	else if (schema_ver > 1)
		g_info("migrating v%u database by altering", schema_ver);

	switch (schema_ver) {
	/* create initial up-to-date database or migrate */
	case 0:
		if (!fu_history_create_database(self, error))
			return FALSE;
		break;
	case 1:
		if (!fu_history_migrate_database_v1(self, error))
			return FALSE;
		break;
	case 2:
		if (!fu_history_migrate_database_v2(self, error))
			return FALSE;
	/* fall through */
	case 3:
		if (!fu_history_migrate_database_v3(self, error))
			return FALSE;
	/* fall through */
	case 4:
		if (!fu_history_migrate_database_v4(self, error))
			return FALSE;
	/* fall through */
	case 5:
		if (!fu_history_migrate_database_v5(self, error))
			return FALSE;
	/* fall through */
	case 6:
		if (!fu_history_migrate_database_v6(self, error))
			return FALSE;
	/* fall through */
	case 7:
		if (!fu_history_migrate_database_v7(self, error))
			return FALSE;
	/* fall through */
	case 8:
		if (!fu_history_migrate_database_v8(self, error))
			return FALSE;
	/* fall through */
	case 9:
	case 10:
		if (!fu_history_migrate_database_v9(self, error))
			return FALSE;
	/* fall through */
	case 11:
		if (!fu_history_migrate_database_v10(self, error))
			return FALSE;
	/* fall through */
	case 12:
		if (!fu_history_migrate_database_v11(self, error))
			return FALSE;
	/* fall through */
	case 13:
		if (!fu_history_migrate_database_v12(self, error))
			return FALSE;
		/* no longer fall through */
		break;
	default:
		/* this is probably okay, but return an error if we ever delete
		 * or rename columns */
		g_warning("schema version %u is unknown", schema_ver);
		return TRUE;
	}

	/* set new schema version */
	rc = sqlite3_prepare_v2(self->db, "UPDATE schema SET version=?1;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL for updating schema: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_int(stmt, 1, FU_HISTORY_CURRENT_SCHEMA_VERSION);
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

static gboolean
fu_history_open(FuHistory *self, const gchar *filename, GError **error)
{
	gint rc;
	g_debug("trying to open database '%s'", filename);
	rc = sqlite3_open(filename, &self->db);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Can't open %s: %s",
			    filename,
			    sqlite3_errmsg(self->db));
		return FALSE;
	}

	/* turn off the lookaside cache */
	sqlite3_db_config(self->db, SQLITE_DBCONFIG_LOOKASIDE, NULL, 0, 0);
	return TRUE;
}

static gboolean
fu_history_load(FuHistory *self, GError **error)
{
	gint rc;
	guint schema_ver;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(GFile) file = NULL;

	/* already done */
	if (self->db != NULL)
		return TRUE;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(self->db == NULL, FALSE);

	/* create directory */
	dirname = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	file = g_file_new_for_path(dirname);
	if (!g_file_query_exists(file, NULL)) {
		if (!g_file_make_directory_with_parents(file, NULL, error))
			return FALSE;
	}

	/* open */
	filename = g_build_filename(dirname, "pending.db", NULL);
	if (!fu_history_open(self, filename, error))
		return FALSE;

	/* check database */
	schema_ver = fu_history_get_schema_version(self);
	if (schema_ver == 0) {
		g_autoptr(sqlite3_stmt) stmt_tmp = NULL;
		rc = sqlite3_prepare_v2(self->db,
					"SELECT * FROM history LIMIT 0;",
					-1,
					&stmt_tmp,
					NULL);
		if (rc == SQLITE_OK)
			schema_ver = 1;
	}

	/* create initial up-to-date database, or migrate */
	g_debug("got schema version of %u", schema_ver);
	if (schema_ver != FU_HISTORY_CURRENT_SCHEMA_VERSION) {
		g_autoptr(GError) error_migrate = NULL;
		if (!fu_history_create_or_migrate(self, schema_ver, &error_migrate)) {
			/* this is fatal to the daemon, so delete the database
			 * and try again with something empty */
			g_warning("failed to migrate %s database: %s",
				  filename,
				  error_migrate->message);
			sqlite3_close(self->db);
			if (g_unlink(filename) != 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "Can't delete %s",
					    filename);
				return FALSE;
			}
			if (!fu_history_open(self, filename, error))
				return FALSE;
			return fu_history_create_database(self, error);
		}
	}

	/* success */
	return TRUE;
}

static gchar *
fu_history_convert_hash_to_string(GHashTable *hash)
{
	GString *str = g_string_new(NULL);
	g_autoptr(GList) keys = g_hash_table_get_keys(hash);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(hash, key);
		if (str->len > 0)
			g_string_append(str, ";");
		g_string_append_printf(str, "%s=%s", key, value);
	}
	return g_string_free(str, FALSE);
}

/* unset some flags we don't want to store */
static FwupdDeviceFlags
fu_history_get_device_flags_filtered(FuDevice *device)
{
	FwupdDeviceFlags flags = fu_device_get_flags(device);
	flags &= ~FWUPD_DEVICE_FLAG_SUPPORTED;
	return flags;
}

/**
 * fu_history_modify_device:
 * @self: a #FuHistory
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Modify a device in the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_modify_device(FuHistory *self, FuDevice *device, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* overwrite entry if it exists */
	g_debug("modifying device %s [%s]", fu_device_get_name(device), fu_device_get_id(device));
	rc = sqlite3_prepare_v2(self->db,
				"UPDATE history SET "
				"update_state = ?1, "
				"update_error = ?2, "
				"checksum_device = ?6, "
				"device_modified = ?7, "
				"install_duration = ?8, "
				"flags = ?3 "
				"WHERE device_id = ?4;",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to update history: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}

	sqlite3_bind_int(stmt, 1, fu_device_get_update_state(device));
	sqlite3_bind_text(stmt, 2, fu_device_get_update_error(device), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 3, fu_history_get_device_flags_filtered(device));
	sqlite3_bind_text(stmt, 4, fu_device_get_id(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 5, fu_device_get_version(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(
	    stmt,
	    6,
	    fwupd_checksum_get_by_kind(fu_device_get_checksums(device), G_CHECKSUM_SHA1),
	    -1,
	    SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 7, fu_device_get_modified_usec(device) / G_USEC_PER_SEC);
	sqlite3_bind_int64(stmt, 8, fu_device_get_install_duration(device));

	if (!fu_history_stmt_exec(self, stmt, NULL, error))
		return FALSE;
	if (sqlite3_changes(self->db) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no device %s",
			    fu_device_get_id(device));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_history_modify_device_release:
 * @self: a #FuHistory
 * @device: a #FuDevice
 * @release: a #FuRelease
 * @error: (nullable): optional return location for an error
 *
 * Modify a device in the history database, also changing metadata from the new release.
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.8.8
 **/
gboolean
fu_history_modify_device_release(FuHistory *self,
				 FuDevice *device,
				 FuRelease *release,
				 GError **error)
{
	gint rc;
	g_autofree gchar *metadata = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* metadata is stored as a simple string */
	metadata = fu_history_convert_hash_to_string(fu_release_get_metadata(release));

	/* overwrite entry if it exists */
	g_debug("modifying device %s [%s]", fu_device_get_name(device), fu_device_get_id(device));
	rc = sqlite3_prepare_v2(self->db,
				"UPDATE history SET "
				"update_state = ?1, "
				"update_error = ?2, "
				"checksum_device = ?6, "
				"device_modified = ?7, "
				"metadata = ?8, "
				"flags = ?3 "
				"WHERE device_id = ?4;",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to update history: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}

	sqlite3_bind_int(stmt, 1, fu_device_get_update_state(device));
	sqlite3_bind_text(stmt, 2, fu_device_get_update_error(device), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 3, fu_history_get_device_flags_filtered(device));
	sqlite3_bind_text(stmt, 4, fu_device_get_id(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 5, fu_device_get_version(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(
	    stmt,
	    6,
	    fwupd_checksum_get_by_kind(fu_device_get_checksums(device), G_CHECKSUM_SHA1),
	    -1,
	    SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 7, fu_device_get_modified_usec(device) / G_USEC_PER_SEC);
	sqlite3_bind_text(stmt, 8, metadata, -1, SQLITE_STATIC);

	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_add_device:
 * @self: a #FuHistory
 * @device: a device
 * @release: a #FuRelease
 * @error: (nullable): optional return location for an error
 *
 * Adds a device to the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_add_device(FuHistory *self, FuDevice *device, FuRelease *release, GError **error)
{
	const gchar *checksum_device;
	const gchar *checksum = NULL;
	gint rc;
	g_autofree gchar *metadata = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(FU_IS_RELEASE(release), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* make tests easier */
	fu_device_convert_instance_ids(device);

	/* ensure all old device(s) with this ID are removed */
	if (!fu_history_remove_device(self, device, error))
		return FALSE;
	g_debug("add device %s [%s]", fu_device_get_name(device), fu_device_get_id(device));
	checksum = fwupd_checksum_get_by_kind(fu_release_get_checksums(release), G_CHECKSUM_SHA1);
	checksum_device =
	    fwupd_checksum_get_by_kind(fu_device_get_checksums(device), G_CHECKSUM_SHA1);

	/* metadata is stored as a simple string */
	metadata = fu_history_convert_hash_to_string(fu_release_get_metadata(release));

	/* add */
	rc = sqlite3_prepare_v2(self->db,
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
				"protocol,"
				"release_id,"
				"appstream_id,"
				"version_format,"
				"install_duration,"
				"release_flags) "
				"VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,"
				"?11,?12,?13,?14,?15,?16,?17,?18,?19,?20,?21)",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to insert history: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, fu_device_get_id(device), -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, fu_device_get_update_state(device));
	sqlite3_bind_text(stmt, 3, fu_device_get_update_error(device), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 4, fu_history_get_device_flags_filtered(device));
	sqlite3_bind_text(stmt, 5, fu_release_get_filename(release), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 6, checksum, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 7, fu_device_get_name(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 8, fu_device_get_plugin(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 9, fu_device_get_guid_default(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 10, metadata, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 11, fu_device_get_created_usec(device) / G_USEC_PER_SEC);
	sqlite3_bind_int64(stmt, 12, fu_device_get_modified_usec(device) / G_USEC_PER_SEC);
	sqlite3_bind_text(stmt, 13, fu_device_get_version(device), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 14, fu_release_get_version(release), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 15, checksum_device, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 16, fu_release_get_protocol(release), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 17, fu_release_get_id(release), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 18, fu_release_get_appstream_id(release), -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 19, fu_device_get_version_format(device));
	sqlite3_bind_int(stmt, 20, fu_device_get_install_duration(device));
	sqlite3_bind_int(stmt, 21, fu_release_get_flags(release));
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_remove_all:
 * @self: a #FuHistory
 * @error: (nullable): optional return location for an error
 *
 * Remove all devices from the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_remove_all(FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* remove entries */
	g_debug("removing all devices");
	rc = sqlite3_prepare_v2(self->db, "DELETE FROM history;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to delete history: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_remove_device:
 * @self: a #FuHistory
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Remove a device from the history database
 *
 * Returns: @TRUE if successful, @FALSE for failure
 *
 * Since: 1.0.4
 **/
gboolean
fu_history_remove_device(FuHistory *self, FuDevice *device, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	g_debug("remove device %s [%s]", fu_device_get_name(device), fu_device_get_id(device));
	rc = sqlite3_prepare_v2(self->db,
				"DELETE FROM history WHERE device_id = ?1;",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to delete history: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, fu_device_get_id(device), -1, SQLITE_STATIC);
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_get_device_by_id:
 * @self: a #FuHistory
 * @device_id: a string
 * @error: (nullable): optional return location for an error
 *
 * Returns the device from the history database or NULL if not found
 *
 * Returns: (transfer full): a device
 *
 * Since: 1.0.4
 **/
FuDevice *
fu_history_get_device_by_id(FuHistory *self, const gchar *device_id, GError **error)
{
	gint rc;
	g_autoptr(GPtrArray) array_tmp = NULL;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);

	/* lazy load */
	if (!fu_history_load(self, error))
		return NULL;

	/* get all the devices */
	rc = sqlite3_prepare_v2(self->db,
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
				"protocol, "
				"release_id, "
				"appstream_id, "
				"version_format, "
				"install_duration, "
				"release_flags FROM history WHERE "
				"device_id = ?1 ORDER BY device_created DESC "
				"LIMIT 1",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to get history: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	sqlite3_bind_text(stmt, 1, device_id, -1, SQLITE_STATIC);
	array_tmp = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	if (!fu_history_stmt_exec(self, stmt, array_tmp, error))
		return NULL;
	if (array_tmp->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "No devices found");
		return NULL;
	}
	return g_object_ref(g_ptr_array_index(array_tmp, 0));
}

/**
 * fu_history_get_devices:
 * @self: a #FuHistory
 * @error: (nullable): optional return location for an error
 *
 * Gets the devices in the history database.
 *
 * Returns: (element-type #FuDevice) (transfer container): devices
 *
 * Since: 1.0.4
 **/
GPtrArray *
fu_history_get_devices(FuHistory *self, GError **error)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(sqlite3_stmt) stmt = NULL;
	gint rc;

	g_return_val_if_fail(FU_IS_HISTORY(self), NULL);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load(self, error))
			return NULL;
	}

	/* get all the devices */
	rc = sqlite3_prepare_v2(self->db,
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
				"protocol, "
				"release_id, "
				"appstream_id, "
				"version_format, "
				"install_duration, "
				"release_flags FROM history "
				"ORDER BY device_modified ASC;",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to get history: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	if (!fu_history_stmt_exec(self, stmt, array, error))
		return NULL;
	return g_steal_pointer(&array);
}

/**
 * fu_history_get_approved_firmware:
 * @self: a #FuHistory
 * @error: (nullable): optional return location for an error
 *
 * Returns approved firmware records.
 *
 * Returns: (transfer full) (element-type gchar *): records
 *
 * Since: 1.2.6
 **/
GPtrArray *
fu_history_get_approved_firmware(FuHistory *self, GError **error)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), NULL);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load(self, error))
			return NULL;
	}

	/* get all the approved firmware */
	rc = sqlite3_prepare_v2(self->db,
				"SELECT checksum FROM approved_firmware;",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to get checksum: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const gchar *tmp = (const gchar *)sqlite3_column_text(stmt, 0);
		g_ptr_array_add(array, g_strdup(tmp));
	}
	if (rc != SQLITE_DONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to execute prepared statement: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	return g_steal_pointer(&array);
}

/**
 * fu_history_clear_approved_firmware:
 * @self: a #FuHistory
 * @error: (nullable): optional return location for an error
 *
 * Clear all approved firmware records
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.2.6
 **/
gboolean
fu_history_clear_approved_firmware(FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* remove entries */
	rc = sqlite3_prepare_v2(self->db, "DELETE FROM approved_firmware;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to delete approved firmware: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_add_approved_firmware:
 * @self: a #FuHistory
 * @checksum: a string
 * @error: (nullable): optional return location for an error
 *
 * Add an approved firmware record to the database
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.2.6
 **/
gboolean
fu_history_add_approved_firmware(FuHistory *self, const gchar *checksum, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(checksum != NULL, FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* add */
	rc = sqlite3_prepare_v2(self->db,
				"INSERT INTO approved_firmware (checksum) "
				"VALUES (?1)",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to insert checksum: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, checksum, -1, SQLITE_STATIC);
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_get_blocked_firmware:
 * @self: a #FuHistory
 * @error: (nullable): optional return location for an error
 *
 * Returns blocked firmware records.
 *
 * Returns: (transfer full) (element-type gchar *): records
 *
 * Since: 1.4.6
 **/
GPtrArray *
fu_history_get_blocked_firmware(FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), NULL);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load(self, error))
			return NULL;
	}

	/* get all the blocked firmware */
	rc =
	    sqlite3_prepare_v2(self->db, "SELECT checksum FROM blocked_firmware;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to get checksum: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const gchar *tmp = (const gchar *)sqlite3_column_text(stmt, 0);
		g_ptr_array_add(array, g_strdup(tmp));
	}
	if (rc != SQLITE_DONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to execute prepared statement: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	return g_steal_pointer(&array);
}

/**
 * fu_history_clear_blocked_firmware:
 * @self: a #FuHistory
 * @error: (nullable): optional return location for an error
 *
 * Clear all blocked firmware records
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.4.6
 **/
gboolean
fu_history_clear_blocked_firmware(FuHistory *self, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* remove entries */
	rc = sqlite3_prepare_v2(self->db, "DELETE FROM blocked_firmware;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to delete blocked firmware: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_add_blocked_firmware:
 * @self: a #FuHistory
 * @checksum: a string
 * @error: (nullable): optional return location for an error
 *
 * Add an blocked firmware record to the database
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.4.6
 **/
gboolean
fu_history_add_blocked_firmware(FuHistory *self, const gchar *checksum, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(checksum != NULL, FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* add */
	rc = sqlite3_prepare_v2(self->db,
				"INSERT INTO blocked_firmware (checksum) "
				"VALUES (?1)",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to insert checksum: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, checksum, -1, SQLITE_STATIC);
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

gboolean
fu_history_add_security_attribute(FuHistory *self,
				  const gchar *security_attr_json,
				  const gchar *hsi_score,
				  GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* remove entries */
	rc = sqlite3_prepare_v2(self->db,
				"INSERT INTO hsi_history (hsi_details, hsi_score)"
				"VALUES (?1, ?2)",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to write security attribute: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, security_attr_json, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, hsi_score, -1, SQLITE_STATIC);
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_get_security_attrs:
 * @self: a #FuHistory
 * @limit: maximum number of attributes to return, or 0 for no limit
 * @error: (nullable): optional return location for an error
 *
 * Gets the security attributes in the history database.
 * Attributes with the same stores JSON data will be deduplicated as required.
 *
 * Returns: (element-type #FuSecurityAttrs) (transfer container): attrs
 *
 * Since: 1.7.1
 **/
GPtrArray *
fu_history_get_security_attrs(FuHistory *self, guint limit, GError **error)
{
	gint rc;
	guint old_hash = 0;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), NULL);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load(self, error))
			return NULL;
	}

	/* get all the devices */
	rc = sqlite3_prepare_v2(self->db,
				"SELECT timestamp, hsi_details FROM hsi_history "
				"ORDER BY timestamp DESC;",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to get security attrs: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const gchar *json;
		guint hash;
		const gchar *timestamp;
		g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
		g_autoptr(GDateTime) created_dt = NULL;
		g_autoptr(GTimeZone) tz_utc = g_time_zone_new_utc();

		/* old */
		timestamp = (const gchar *)sqlite3_column_text(stmt, 0);
		if (timestamp == NULL)
			continue;

		/* device_id */
		json = (const gchar *)sqlite3_column_text(stmt, 1);
		if (json == NULL)
			continue;

		/* do not create dups */
		hash = g_str_hash(json);
		if (hash == old_hash) {
			g_debug("skipping %s as unchanged", timestamp);
			continue;
		}
		old_hash = hash;

		/* parse JSON */
		g_debug("parsing %s", timestamp);
		if (!fwupd_codec_from_json_string(FWUPD_CODEC(attrs), json, error))
			return NULL;

		/* parse timestamp */
		created_dt = g_date_time_new_from_iso8601(timestamp, tz_utc);
		if (created_dt != NULL) {
			guint64 created_unix = g_date_time_to_unix(created_dt);
			g_autoptr(GPtrArray) attr_array = fu_security_attrs_get_all(attrs, NULL);
			for (guint i = 0; i < attr_array->len; i++) {
				FwupdSecurityAttr *attr = g_ptr_array_index(attr_array, i);
				fwupd_security_attr_set_created(attr, created_unix);
			}
		}

		/* success */
		g_ptr_array_add(array, g_steal_pointer(&attrs));
		if (limit > 0 && array->len >= limit) {
			rc = SQLITE_DONE;
			break;
		}
	}
	if (rc != SQLITE_DONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to execute prepared statement: %s",
			    sqlite3_errmsg(self->db));
		return NULL;
	}
	return g_steal_pointer(&array);
}

/**
 * fu_history_has_emulation_tag:
 * @self: a #FuHistory
 * @device_id: (nullable): a device ID, or %NULL for "any"
 * @error: (nullable): optional return location for an error
 *
 * Returns if the device has been tagged for emulation.
 *
 * Returns: %TRUE if tagged
 *
 * Since: 2.0.1
 **/
gboolean
fu_history_has_emulation_tag(FuHistory *self, const gchar *device_id, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);

	/* lazy load */
	if (self->db == NULL) {
		if (!fu_history_load(self, error))
			return FALSE;
	}

	/* get tagged device ID */
	if (device_id != NULL) {
		rc = sqlite3_prepare_v2(self->db,
					"SELECT device_id FROM emulation_tag "
					"WHERE device_id = ?1 LIMIT 1;",
					-1,
					&stmt,
					NULL);
	} else {
		rc = sqlite3_prepare_v2(self->db,
					"SELECT device_id FROM emulation_tag LIMIT 1;",
					-1,
					&stmt,
					NULL);
	}
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to prepare SQL to get emulation tag: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, device_id, -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		if (device_id == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "no devices were found for emulation tag");
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "%s was not found for emulation tag",
			    device_id);
		return FALSE;
	}
	if (rc != SQLITE_ROW) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to execute prepared statement: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_history_add_emulation_tag:
 * @self: a #FuHistory
 * @device_id: a device ID
 * @error: (nullable): optional return location for an error
 *
 * Add a hash of a device to be tagged for emulation to the database
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 2.0.1
 **/
gboolean
fu_history_add_emulation_tag(FuHistory *self, const gchar *device_id, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* add */
	rc = sqlite3_prepare_v2(self->db,
				"INSERT INTO emulation_tag (device_id) "
				"VALUES (?1)",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to prepare SQL to insert emulation tag: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, device_id, -1, SQLITE_STATIC);
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

/**
 * fu_history_remove_emulation_tag:
 * @self: a #FuHistory
 * @error: (nullable): optional return location for an error
 *
 * Remove a hash of a device to be tagged for emulation from the database
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 2.0.1
 **/
gboolean
fu_history_remove_emulation_tag(FuHistory *self, const gchar *device_id, GError **error)
{
	gint rc;
	g_autoptr(sqlite3_stmt) stmt = NULL;

	g_return_val_if_fail(FU_IS_HISTORY(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);

	/* lazy load */
	if (!fu_history_load(self, error))
		return FALSE;

	/* remove entries */
	rc = sqlite3_prepare_v2(self->db,
				"DELETE FROM emulation_tag WHERE device_id = ?1;",
				-1,
				&stmt,
				NULL);
	if (rc != SQLITE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to prepare SQL to delete emulation tag: %s",
			    sqlite3_errmsg(self->db));
		return FALSE;
	}
	sqlite3_bind_text(stmt, 1, device_id, -1, SQLITE_STATIC);
	return fu_history_stmt_exec(self, stmt, NULL, error);
}

static void
fu_history_housekeeping_cb(FuContext *ctx, FuHistory *self)
{
	sqlite3_release_memory(G_MAXINT32);
	if (self->db != NULL)
		sqlite3_db_release_memory(self->db);
}

static void
fu_history_dispose(GObject *object)
{
	FuHistory *self = FU_HISTORY(object);
	if (self->ctx != NULL)
		g_signal_handlers_disconnect_by_data(self->ctx, self);
	g_clear_object(&self->ctx);
	G_OBJECT_CLASS(fu_history_parent_class)->dispose(object);
}

static void
fu_history_class_init(FuHistoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_history_finalize;
	object_class->dispose = fu_history_dispose;
}

static void
fu_history_init(FuHistory *self)
{
}

static void
fu_history_finalize(GObject *object)
{
	FuHistory *self = FU_HISTORY(object);
	if (self->db != NULL)
		sqlite3_close(self->db);
	G_OBJECT_CLASS(fu_history_parent_class)->finalize(object);
}

/**
 * fu_history_new:
 *
 * Creates a new #FuHistory
 *
 * Since: 1.0.4
 **/
FuHistory *
fu_history_new(FuContext *ctx)
{
	FuHistory *self;
	self = g_object_new(FU_TYPE_PENDING, NULL);
	self->ctx = g_object_ref(ctx);
	g_signal_connect(self->ctx, "housekeeping", G_CALLBACK(fu_history_housekeeping_cb), self);
	return FU_HISTORY(self);
}
