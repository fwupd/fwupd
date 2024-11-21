/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuRemote"

#include "config.h"

#include "fwupd-remote-private.h"

#include "fu-remote.h"

struct _FuRemote {
	FwupdRemote parent_instance;
};

G_DEFINE_TYPE(FuRemote, fu_remote, FWUPD_TYPE_REMOTE)

#define FWUPD_REMOTE_CONFIG_DEFAULT_REFRESH_INTERVAL 86400 /* 24h */

/**
 * fu_remote_load_from_filename:
 * @self: a #FwupdRemote
 * @filename: (not nullable): a filename
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Loads metadata about the remote from a keyfile.
 * This can be called zero or multiple times for each remote.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_remote_load_from_filename(FwupdRemote *self,
			     const gchar *filename,
			     GCancellable *cancellable,
			     GError **error)
{
	const gchar *group = "fwupd Remote";
	g_autofree gchar *id = NULL;
	g_autoptr(GKeyFile) kf = NULL;

	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* set ID */
	id = g_path_get_basename(filename);
	fwupd_remote_set_id(self, id);

	/* load file */
	kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, filename, G_KEY_FILE_NONE, error))
		return FALSE;

	/* the first remote sets the URI, even if it's file:// to the cache */
	if (g_key_file_has_key(kf, group, "MetadataURI", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "MetadataURI", NULL);
		if (g_str_has_prefix(tmp, "file://")) {
			const gchar *filename_cache = tmp;
			if (g_str_has_prefix(filename_cache, "file://"))
				filename_cache += 7;
			if (g_file_test(filename_cache, G_FILE_TEST_IS_DIR))
				fwupd_remote_set_kind(self, FWUPD_REMOTE_KIND_DIRECTORY);
			else
				fwupd_remote_set_kind(self, FWUPD_REMOTE_KIND_LOCAL);
			fwupd_remote_set_filename_cache(self, filename_cache);
		} else if (g_str_has_prefix(tmp, "http://") || g_str_has_prefix(tmp, "https://") ||
			   g_str_has_prefix(tmp, "ipfs://") || g_str_has_prefix(tmp, "ipns://")) {
			fwupd_remote_set_kind(self, FWUPD_REMOTE_KIND_DOWNLOAD);
			fwupd_remote_set_refresh_interval(
			    self,
			    FWUPD_REMOTE_CONFIG_DEFAULT_REFRESH_INTERVAL);
			fwupd_remote_set_metadata_uri(self, tmp);
		}
	}

	/* all keys are optional */
	if (g_key_file_has_key(kf, group, "Enabled", NULL)) {
		if (g_key_file_get_boolean(kf, group, "Enabled", NULL))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_ENABLED);
		else
			fwupd_remote_remove_flag(self, FWUPD_REMOTE_FLAG_ENABLED);
	}
	if (g_key_file_has_key(kf, group, "ApprovalRequired", NULL)) {
		if (g_key_file_get_boolean(kf, group, "ApprovalRequired", NULL))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED);
		else
			fwupd_remote_remove_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED);
	}
	if (g_key_file_has_key(kf, group, "Title", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "Title", NULL);
		fwupd_remote_set_title(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "PrivacyURI", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "PrivacyURI", NULL);
		fwupd_remote_set_privacy_uri(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "RefreshInterval", NULL)) {
		fwupd_remote_set_refresh_interval(
		    self,
		    g_key_file_get_uint64(kf, group, "RefreshInterval", NULL));
	}
	if (g_key_file_has_key(kf, group, "ReportURI", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "ReportURI", NULL);
		fwupd_remote_set_report_uri(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "Username", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "Username", NULL);
		fwupd_remote_set_username(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "Password", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "Password", NULL);
		fwupd_remote_set_password(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "FirmwareBaseURI", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "FirmwareBaseURI", NULL);
		fwupd_remote_set_firmware_base_uri(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "OrderBefore", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "OrderBefore", NULL);
		fwupd_remote_set_order_before(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "OrderAfter", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "OrderAfter", NULL);
		fwupd_remote_set_order_after(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "AutomaticReports", NULL)) {
		if (g_key_file_get_boolean(kf, group, "AutomaticReports", NULL))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS);
		else
			fwupd_remote_remove_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS);
	}
	if (g_key_file_has_key(kf, group, "AutomaticSecurityReports", NULL)) {
		if (g_key_file_get_boolean(kf, group, "AutomaticSecurityReports", NULL))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS);
		else
			fwupd_remote_remove_flag(self,
						 FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS);
	}

	/* old versions of fwupd used empty strings to mean "unset" and the package manager
	 * might not have replaced the file marked as a config file due to modification */
	if (g_strcmp0(fwupd_remote_get_username(self), "") == 0 &&
	    g_strcmp0(fwupd_remote_get_password(self), "") == 0) {
		fwupd_remote_set_username(self, NULL);
		fwupd_remote_set_password(self, NULL);
	}

	/* success */
	fwupd_remote_set_filename_source(self, filename);
	return TRUE;
}

/**
 * fu_remote_save_to_filename:
 * @self: a #FwupdRemote
 * @filename: (not nullable): a filename
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Saves metadata about the remote to a keyfile.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_remote_save_to_filename(FwupdRemote *self,
			   const gchar *filename,
			   GCancellable *cancellable,
			   GError **error)
{
	const gchar *group = "fwupd Remote";
	g_autoptr(GKeyFile) kf = g_key_file_new();

	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optional keys */
	if (fwupd_remote_get_metadata_uri(self) != NULL)
		g_key_file_set_string(kf,
				      group,
				      "MetadataURI",
				      fwupd_remote_get_metadata_uri(self));
	if (fwupd_remote_get_title(self) != NULL)
		g_key_file_set_string(kf, group, "Title", fwupd_remote_get_title(self));
	if (fwupd_remote_get_privacy_uri(self) != NULL)
		g_key_file_set_string(kf, group, "PrivacyURI", fwupd_remote_get_privacy_uri(self));
	if (fwupd_remote_get_report_uri(self) != NULL)
		g_key_file_set_string(kf, group, "ReportURI", fwupd_remote_get_report_uri(self));
	if (fwupd_remote_get_refresh_interval(self) != 0)
		g_key_file_set_uint64(kf,
				      group,
				      "RefreshInterval",
				      fwupd_remote_get_refresh_interval(self));
	if (fwupd_remote_get_username(self) != NULL)
		g_key_file_set_string(kf, group, "Username", fwupd_remote_get_username(self));
	if (fwupd_remote_get_password(self) != NULL)
		g_key_file_set_string(kf, group, "Password", fwupd_remote_get_password(self));
	if (fwupd_remote_get_firmware_base_uri(self) != NULL)
		g_key_file_set_string(kf,
				      group,
				      "FirmwareBaseURI",
				      fwupd_remote_get_firmware_base_uri(self));
	if (fwupd_remote_get_order_after(self) != NULL) {
		g_autofree gchar *str = g_strjoinv(";", fwupd_remote_get_order_after(self));
		g_key_file_set_string(kf, group, "OrderAfter", str);
	}
	if (fwupd_remote_get_order_before(self) != NULL) {
		g_autofree gchar *str = g_strjoinv(";", fwupd_remote_get_order_before(self));
		g_key_file_set_string(kf, group, "OrderBefore", str);
	}
	if (fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_ENABLED))
		g_key_file_set_boolean(kf, group, "Enabled", TRUE);
	if (fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED))
		g_key_file_set_boolean(kf, group, "ApprovalRequired", TRUE);
	if (fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS))
		g_key_file_set_boolean(kf, group, "AutomaticReports", TRUE);
	if (fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS))
		g_key_file_set_boolean(kf, group, "AutomaticSecurityReports", TRUE);

	/* save file */
	return g_key_file_save_to_file(kf, filename, error);
}

static void
fu_remote_init(FuRemote *self)
{
}

static void
fu_remote_class_init(FuRemoteClass *klass)
{
}

FwupdRemote *
fu_remote_new(void)
{
	return FWUPD_REMOTE(g_object_new(FU_TYPE_REMOTE, NULL));
}
