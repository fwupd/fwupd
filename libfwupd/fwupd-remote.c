/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <libsoup/soup.h>

#include "fwupd-deprecated.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-remote-private.h"

/**
 * SECTION:fwupd-remote
 * @short_description: a source of firmware
 *
 * An object that represents a source of metadata that provides firmware.
 *
 * See also: #FwupdClient
 */

static void fwupd_remote_finalize	 (GObject *obj);

typedef struct {
	GObject			 parent_instance;
	FwupdRemoteKind		 kind;
	FwupdKeyringKind	 keyring_kind;
	gchar			*id;
	gchar			*firmware_base_uri;
	gchar			*report_uri;
	gchar			*metadata_uri;
	gchar			*metadata_uri_sig;
	gchar			*username;
	gchar			*password;
	gchar			*title;
	gchar			*agreement;
	gchar			*checksum;
	gchar			*filename_cache;
	gchar			*filename_cache_sig;
	gchar			*filename_source;
	gboolean		 enabled;
	gboolean		 approval_required;
	gint			 priority;
	guint64			 mtime;
	gchar			**order_after;
	gchar			**order_before;
	gchar			*remotes_dir;
	gboolean		 automatic_reports;
} FwupdRemotePrivate;

enum {
	PROP_0,
	PROP_ID,
	PROP_ENABLED,
	PROP_APPROVAL_REQUIRED,
	PROP_AUTOMATIC_REPORTS,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (FwupdRemote, fwupd_remote, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_remote_get_instance_private (o))

static void
fwupd_remote_set_username (FwupdRemote *self, const gchar *username)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	if (username != NULL && username[0] == '\0')
		username = NULL;
	priv->username = g_strdup (username);
}

static void
fwupd_remote_set_title (FwupdRemote *self, const gchar *title)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_free (priv->title);
	priv->title = g_strdup (title);
}

/**
 * fwupd_remote_set_agreement:
 * @self: A #FwupdRemote
 * @agreement: Agreement markup
 *
 * Sets the remote agreement in AppStream markup format
 *
 * Since: 1.0.7
 **/
void
fwupd_remote_set_agreement (FwupdRemote *self, const gchar *agreement)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_free (priv->agreement);
	priv->agreement = g_strdup (agreement);
}

static void
fwupd_remote_set_checksum (FwupdRemote *self, const gchar *checksum)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_free (priv->checksum);
	priv->checksum = g_strdup (checksum);
}

static void
fwupd_remote_set_password (FwupdRemote *self, const gchar *password)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	if (password != NULL && password[0] == '\0')
		password = NULL;
	priv->password = g_strdup (password);
}

static void
fwupd_remote_set_kind (FwupdRemote *self, FwupdRemoteKind kind)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	priv->kind = kind;
}

static void
fwupd_remote_set_keyring_kind (FwupdRemote *self, FwupdKeyringKind keyring_kind)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	priv->keyring_kind = keyring_kind;
}

/* note, this has to be set before url */
static void
fwupd_remote_set_id (FwupdRemote *self, const gchar *id)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_free (priv->id);
	priv->id = g_strdup (id);
	g_strdelimit (priv->id, ".", '\0');
}

static void
fwupd_remote_set_filename_source (FwupdRemote *self, const gchar *filename_source)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_free (priv->filename_source);
	priv->filename_source = g_strdup (filename_source);
}

static const gchar *
fwupd_remote_get_suffix_for_keyring_kind (FwupdKeyringKind keyring_kind)
{
	if (keyring_kind == FWUPD_KEYRING_KIND_GPG)
		return ".asc";
	if (keyring_kind == FWUPD_KEYRING_KIND_PKCS7)
		return ".p7b";
	return NULL;
}

static SoupURI *
fwupd_remote_build_uri (FwupdRemote *self, const gchar *url, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	SoupURI *uri;

	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	g_return_val_if_fail (url != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create URI, substituting if required */
	if (priv->firmware_base_uri != NULL) {
		g_autoptr(SoupURI) uri_tmp = NULL;
		g_autofree gchar *basename = NULL;
		g_autofree gchar *url2 = NULL;
		uri_tmp = soup_uri_new (url);
		if (uri_tmp == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse URI '%s'", url);
			return NULL;
		}
		basename = g_path_get_basename (soup_uri_get_path (uri_tmp));
		url2 = g_build_filename (priv->firmware_base_uri, basename, NULL);
		uri = soup_uri_new (url2);
		if (uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse URI '%s'", url2);
			return NULL;
		}

	/* use the base URI of the metadata to build the full path */
	} else if (g_strstr_len (url, -1, "/") == NULL) {
		g_autofree gchar *basename = NULL;
		g_autofree gchar *path = NULL;
		uri = soup_uri_new (priv->metadata_uri);
		if (uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse metadata URI '%s'", url);
			return NULL;
		}
		basename = g_path_get_dirname (soup_uri_get_path (uri));
		path = g_build_filename (basename, url, NULL);
		soup_uri_set_path (uri, path);

	/* a normal URI */
	} else {
		uri = soup_uri_new (url);
		if (uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse URI '%s'", url);
			return NULL;
		}
	}

	/* set the username and password */
	if (priv->username != NULL)
		soup_uri_set_user (uri, priv->username);
	if (priv->password != NULL)
		soup_uri_set_password (uri, priv->password);
	return uri;
}

/* note, this has to be set before username and password */
static void
fwupd_remote_set_metadata_uri (FwupdRemote *self, const gchar *metadata_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	const gchar *suffix;
	g_autoptr(SoupURI) uri = NULL;

	/* build the URI */
	uri = soup_uri_new (metadata_uri);
	if (uri == NULL)
		return;

	/* save this so we can export the object as a GVariant */
	priv->metadata_uri = g_strdup (metadata_uri);

	/* generate the signature URI too */
	suffix = fwupd_remote_get_suffix_for_keyring_kind (priv->keyring_kind);
	if (suffix != NULL)
		priv->metadata_uri_sig = g_strconcat (metadata_uri, suffix, NULL);
}

/* note, this has to be set after MetadataURI */
static void
fwupd_remote_set_firmware_base_uri (FwupdRemote *self, const gchar *firmware_base_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	priv->firmware_base_uri = g_strdup (firmware_base_uri);
}

static void
fwupd_remote_set_report_uri (FwupdRemote *self, const gchar *report_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	priv->report_uri = g_strdup (report_uri);
}

/**
 * fwupd_remote_kind_from_string:
 * @kind: a string, e.g. `download`
 *
 * Converts an printable string to an enumerated type.
 *
 * Returns: a #FwupdRemoteKind, e.g. %FWUPD_REMOTE_KIND_DOWNLOAD
 *
 * Since: 0.9.6
 **/
FwupdRemoteKind
fwupd_remote_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "download") == 0)
		return FWUPD_REMOTE_KIND_DOWNLOAD;
	if (g_strcmp0 (kind, "local") == 0)
		return FWUPD_REMOTE_KIND_LOCAL;
	if (g_strcmp0 (kind, "directory") == 0)
		return FWUPD_REMOTE_KIND_DIRECTORY;
	return FWUPD_REMOTE_KIND_UNKNOWN;
}

/**
 * fwupd_remote_kind_to_string:
 * @kind: a #FwupdRemoteKind, e.g. %FWUPD_REMOTE_KIND_DOWNLOAD
 *
 * Converts an enumerated type to a printable string.
 *
 * Returns: a string, e.g. `download`
 *
 * Since: 0.9.6
 **/
const gchar *
fwupd_remote_kind_to_string (FwupdRemoteKind kind)
{
	if (kind == FWUPD_REMOTE_KIND_DOWNLOAD)
		return "download";
	if (kind == FWUPD_REMOTE_KIND_LOCAL)
		return "local";
	if (kind == FWUPD_REMOTE_KIND_DIRECTORY)
		return "directory";
	return NULL;
}

static void
fwupd_remote_set_filename_cache (FwupdRemote *self, const gchar *filename)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	const gchar *suffix;

	g_return_if_fail (FWUPD_IS_REMOTE (self));

	g_free (priv->filename_cache);
	priv->filename_cache = g_strdup (filename);

	/* create for all remote types */
	suffix = fwupd_remote_get_suffix_for_keyring_kind (priv->keyring_kind);
	if (suffix != NULL) {
		g_free (priv->filename_cache_sig);
		priv->filename_cache_sig = g_strconcat (filename, suffix, NULL);
	}
}

/**
 * fwupd_remote_load_from_filename:
 * @self: A #FwupdRemote
 * @filename: A filename
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Sets up the remote ready for use. Most other methods call this
 * for you, and do you only need to call this if you are just watching
 * the self.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.9.3
 **/
gboolean
fwupd_remote_load_from_filename (FwupdRemote *self,
				 const gchar *filename,
				 GCancellable *cancellable,
				 GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	const gchar *group = "fwupd Remote";
	g_autofree gchar *firmware_base_uri = NULL;
	g_autofree gchar *id = NULL;
	g_autofree gchar *keyring_kind = NULL;
	g_autofree gchar *metadata_uri = NULL;
	g_autofree gchar *order_after = NULL;
	g_autofree gchar *order_before = NULL;
	g_autofree gchar *report_uri = NULL;
	g_autoptr(GKeyFile) kf = NULL;

	g_return_val_if_fail (FWUPD_IS_REMOTE (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* set ID */
	id = g_path_get_basename (filename);
	fwupd_remote_set_id (self, id);

	/* load file */
	kf = g_key_file_new ();
	if (!g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, error))
		return FALSE;

	/* get verification type, falling back to GPG */
	keyring_kind = g_key_file_get_string (kf, group, "Keyring", NULL);
	if (keyring_kind == NULL) {
		priv->keyring_kind = FWUPD_KEYRING_KIND_GPG;
	} else {
		priv->keyring_kind = fwupd_keyring_kind_from_string (keyring_kind);
		if (priv->keyring_kind == FWUPD_KEYRING_KIND_UNKNOWN) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse type '%s'",
				     keyring_kind);
			return FALSE;
		}
	}

	/* all remotes need a URI, even if it's file:// to the cache */
	metadata_uri = g_key_file_get_string (kf, group, "MetadataURI", error);
	if (metadata_uri == NULL)
		return FALSE;
	if (g_str_has_prefix (metadata_uri, "file://")) {
		const gchar *filename_cache = metadata_uri;
		if (g_str_has_prefix (filename_cache, "file://"))
			filename_cache += 7;
		fwupd_remote_set_filename_cache (self, filename_cache);
		if (g_file_test (filename_cache, G_FILE_TEST_IS_DIR))
			priv->kind = FWUPD_REMOTE_KIND_DIRECTORY;
		else
			priv->kind = FWUPD_REMOTE_KIND_LOCAL;
	} else if (g_str_has_prefix (metadata_uri, "http://") ||
		   g_str_has_prefix (metadata_uri, "https://")) {
		priv->kind = FWUPD_REMOTE_KIND_DOWNLOAD;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse MetadataURI type '%s'",
			     metadata_uri);
		return FALSE;
	}

	/* extract data */
	priv->enabled = g_key_file_get_boolean (kf, group, "Enabled", NULL);
	priv->approval_required = g_key_file_get_boolean (kf, group, "ApprovalRequired", NULL);
	priv->title = g_key_file_get_string (kf, group, "Title", NULL);

	/* reporting is optional */
	report_uri = g_key_file_get_string (kf, group, "ReportURI", NULL);
	if (report_uri != NULL && report_uri[0] != '\0')
		fwupd_remote_set_report_uri (self, report_uri);

	/* automatic report uploading */
	priv->automatic_reports = g_key_file_get_boolean (kf, group, "AutomaticReports", NULL);

	/* DOWNLOAD-type remotes */
	if (priv->kind == FWUPD_REMOTE_KIND_DOWNLOAD) {
		g_autofree gchar *filename_cache = NULL;
		g_autofree gchar *username = NULL;
		g_autofree gchar *password = NULL;

		/* the client has to download this and the signature */
		fwupd_remote_set_metadata_uri (self, metadata_uri);

		/* check the URI was valid */
		if (priv->metadata_uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse URI '%s' in %s",
				     metadata_uri, filename);
			return FALSE;
		}

		/* username and password are optional */
		username = g_key_file_get_string (kf, group, "Username", NULL);
		if (username != NULL)
			fwupd_remote_set_username (self, username);
		password = g_key_file_get_string (kf, group, "Password", NULL);
		if (password != NULL)
			fwupd_remote_set_password (self, password);

		if (priv->remotes_dir == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Remotes directory not set");
			return FALSE;
		}
		/* set cache to /var/lib... */
		filename_cache = g_build_filename (priv->remotes_dir,
						   priv->id,
						   "metadata.xml.gz",
						   NULL);
		fwupd_remote_set_filename_cache (self, filename_cache);
	}

	/* load the checksum */
	if (priv->filename_cache_sig != NULL &&
	    g_file_test (priv->filename_cache_sig, G_FILE_TEST_EXISTS)) {
		gsize sz = 0;
		g_autofree gchar *buf = NULL;
		g_autoptr(GChecksum) checksum = g_checksum_new (G_CHECKSUM_SHA256);
		if (!g_file_get_contents (priv->filename_cache_sig, &buf, &sz, error)) {
			g_prefix_error (error, "failed to get checksum: ");
			return FALSE;
		}
		g_checksum_update (checksum, (guchar *) buf, (gssize) sz);
		fwupd_remote_set_checksum (self, g_checksum_get_string (checksum));
	} else {
		fwupd_remote_set_checksum (self, NULL);
	}

	/* the base URI is optional */
	firmware_base_uri = g_key_file_get_string (kf, group, "FirmwareBaseURI", NULL);
	if (firmware_base_uri != NULL)
		fwupd_remote_set_firmware_base_uri (self, firmware_base_uri);

	/* some validation around DIRECTORY types */
	if (priv->kind == FWUPD_REMOTE_KIND_DIRECTORY) {
		if (priv->keyring_kind != FWUPD_KEYRING_KIND_NONE) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Keyring kind %s is not supported with directory remote",
				     fwupd_keyring_kind_to_string (priv->keyring_kind));
			return FALSE;
		}
		if (firmware_base_uri != NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Directory remotes don't support firmware base URI");
			return FALSE;
		}
	}

	/* dep logic */
	order_before = g_key_file_get_string (kf, group, "OrderBefore", NULL);
	if (order_before != NULL)
		priv->order_before = g_strsplit_set (order_before, ",:;", -1);
	order_after = g_key_file_get_string (kf, group, "OrderAfter", NULL);
	if (order_after != NULL)
		priv->order_after = g_strsplit_set (order_after, ",:;", -1);

	/* success */
	fwupd_remote_set_filename_source (self, filename);
	return TRUE;
}

/**
 * fwupd_remote_get_order_after:
 * @self: A #FwupdRemote
 *
 * Gets the list of remotes this plugin should be ordered after.
 *
 * Returns: (transfer none): an array
 *
 * Since: 0.9.5
 **/
gchar **
fwupd_remote_get_order_after (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->order_after;
}

/**
 * fwupd_remote_get_order_before:
 * @self: A #FwupdRemote
 *
 * Gets the list of remotes this plugin should be ordered before.
 *
 * Returns: (transfer none): an array
 *
 * Since: 0.9.5
 **/
gchar **
fwupd_remote_get_order_before (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->order_before;
}

/**
 * fwupd_remote_get_filename_cache:
 * @self: A #FwupdRemote
 *
 * Gets the path and filename that the remote is using for a cache.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.6
 **/
const gchar *
fwupd_remote_get_filename_cache (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->filename_cache;
}

/**
 * fwupd_remote_get_filename_cache_sig:
 * @self: A #FwupdRemote
 *
 * Gets the path and filename that the remote is using for a signature cache.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_filename_cache_sig (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->filename_cache_sig;
}

/**
 * fwupd_remote_get_filename_source:
 * @self: A #FwupdRemote
 *
 * Gets the path and filename of the remote itself, typically a `.conf` file.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.8
 **/
const gchar *
fwupd_remote_get_filename_source (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->filename_source;
}

/**
 * fwupd_remote_get_priority:
 * @self: A #FwupdRemote
 *
 * Gets the priority of the remote, where bigger numbers are better.
 *
 * Returns: a priority, or 0 for the default value
 *
 * Since: 0.9.5
 **/
gint
fwupd_remote_get_priority (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	return priv->priority;
}

/**
 * fwupd_remote_get_kind:
 * @self: A #FwupdRemote
 *
 * Gets the kind of the remote.
 *
 * Returns: a #FwupdRemoteKind, e.g. #FWUPD_REMOTE_KIND_LOCAL
 *
 * Since: 0.9.6
 **/
FwupdRemoteKind
fwupd_remote_get_kind (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	return priv->kind;
}

/**
 * fwupd_remote_get_keyring_kind:
 * @self: A #FwupdRemote
 *
 * Gets the keyring kind of the remote.
 *
 * Returns: a #FwupdKeyringKind, e.g. #FWUPD_KEYRING_KIND_GPG
 *
 * Since: 0.9.7
 **/
FwupdKeyringKind
fwupd_remote_get_keyring_kind (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	return priv->keyring_kind;
}

/**
 * fwupd_remote_get_age:
 * @self: A #FwupdRemote
 *
 * Gets the age of the remote in seconds.
 *
 * Returns: a age, or %G_MAXUINT64 for unavailable
 *
 * Since: 0.9.5
 **/
guint64
fwupd_remote_get_age (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	guint64 now;
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
	if (priv->mtime > now)
		return G_MAXUINT64;
	return now - priv->mtime;
}

/**
 * fwupd_remote_set_remotes_dir:
 * @self: A #FwupdRemote
 * @directory: Remotes directory
 *
 * Sets the directory to store remote data
 *
 * Since: 1.3.1
 **/
void
fwupd_remote_set_remotes_dir (FwupdRemote *self, const gchar *directory)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_REMOTE (self));
	g_free (priv->remotes_dir);
	priv->remotes_dir = g_strdup (directory);
}

/**
 * fwupd_remote_set_priority:
 * @self: A #FwupdRemote
 * @priority: an integer, where 1 is better
 *
 * Sets the plugin priority.
 *
 * Since: 0.9.5
 **/
void
fwupd_remote_set_priority (FwupdRemote *self, gint priority)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_REMOTE (self));
	priv->priority = priority;
}

/**
 * fwupd_remote_set_mtime:
 * @self: A #FwupdRemote
 * @mtime: a UNIX itmestamp
 *
 * Sets the plugin modification time.
 *
 * Since: 0.9.5
 **/
void
fwupd_remote_set_mtime (FwupdRemote *self, guint64 mtime)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_REMOTE (self));
	priv->mtime = mtime;
}

/**
 * fwupd_remote_get_username:
 * @self: A #FwupdRemote
 *
 * Gets the username configured for the remote.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.5
 **/
const gchar *
fwupd_remote_get_username (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->username;
}

/**
 * fwupd_remote_get_password:
 * @self: A #FwupdRemote
 *
 * Gets the password configured for the remote.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.5
 **/
const gchar *
fwupd_remote_get_password (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->password;
}

/**
 * fwupd_remote_get_title:
 * @self: A #FwupdRemote
 *
 * Gets the remote title, e.g. `Linux Vendor Firmware Service`.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 0.9.8
 **/
const gchar *
fwupd_remote_get_title (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->title;
}

/**
 * fwupd_remote_get_agreement:
 * @self: A #FwupdRemote
 *
 * Gets the remote agreement in AppStream markup format
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.0.7
 **/
const gchar *
fwupd_remote_get_agreement (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->agreement;
}

/**
 * fwupd_remote_get_remotes_dir:
 * @self: A #FwupdRemote
 *
 * Gets the base directory for storing remote metadata
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.3.1
 **/
const gchar *
fwupd_remote_get_remotes_dir (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->remotes_dir;
}

/**
 * fwupd_remote_get_checksum:
 * @self: A #FwupdRemote
 *
 * Gets the remote checksum.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.0.0
 **/
const gchar *
fwupd_remote_get_checksum (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->checksum;
}

/**
 * fwupd_remote_build_firmware_uri:
 * @self: A #FwupdRemote
 * @url: the URL to use
 * @error: the #GError, or %NULL
 *
 * Builds a URI for the URL using the username and password set for the remote,
 * including any basename URI substitution.
 *
 * Returns: (transfer full): a URI, or %NULL for error
 *
 * Since: 0.9.7
 **/
gchar *
fwupd_remote_build_firmware_uri (FwupdRemote *self, const gchar *url, GError **error)
{
	g_autoptr(SoupURI) uri = fwupd_remote_build_uri (self, url, error);
	if (uri == NULL)
		return NULL;
	return soup_uri_to_string (uri, FALSE);
}

/**
 * fwupd_remote_get_report_uri:
 * @self: A #FwupdRemote
 *
 * Gets the URI for the remote reporting.
 *
 * Returns: (transfer none): a URI, or %NULL for invalid.
 *
 * Since: 1.0.4
 **/
const gchar *
fwupd_remote_get_report_uri (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->report_uri;
}

/**
 * fwupd_remote_get_metadata_uri:
 * @self: A #FwupdRemote
 *
 * Gets the URI for the remote metadata.
 *
 * Returns: (transfer none): a URI, or %NULL for invalid.
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_metadata_uri (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->metadata_uri;
}

/**
 * fwupd_remote_get_metadata_uri_sig:
 * @self: A #FwupdRemote
 *
 * Gets the URI for the remote metadata signature.
 *
 * Returns: (transfer none): a URI, or %NULL for invalid.
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_metadata_uri_sig (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->metadata_uri_sig;
}

/**
 * fwupd_remote_get_firmware_base_uri:
 * @self: A #FwupdRemote
 *
 * Gets the base URI for firmware.
 *
 * Returns: (transfer none): a URI, or %NULL for unset.
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_firmware_base_uri (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->firmware_base_uri;
}

/**
 * fwupd_remote_get_enabled:
 * @self: A #FwupdRemote
 *
 * Gets if the remote is enabled and should be used.
 *
 * Returns: a #TRUE if the remote is enabled
 *
 * Since: 0.9.3
 **/
gboolean
fwupd_remote_get_enabled (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), FALSE);
	return priv->enabled;
}

/**
 * fwupd_remote_get_automatic_reports:
 * @self: A #FwupdRemote
 *
 * Gets if reports should be automatically uploaded to this remote
 *
 * Returns: a #TRUE if the remote should have reports uploaded automatically
 *
 * Since: 1.3.3
 **/
gboolean
fwupd_remote_get_automatic_reports (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), FALSE);
	return priv->automatic_reports;
}

/**
 * fwupd_remote_get_approval_required:
 * @self: A #FwupdRemote
 *
 * Gets if firmware from the remote should be checked against the list
 * of a approved checksums.
 *
 * Returns: a #TRUE if the remote is restricted
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_remote_get_approval_required (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), FALSE);
	return priv->approval_required;
}

/**
 * fwupd_remote_get_id:
 * @self: A #FwupdRemote
 *
 * Gets the remote ID, e.g. `lvfs-testing`.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_remote_get_id (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return priv->id;
}

static void
fwupd_remote_set_from_variant_iter (FwupdRemote *self, GVariantIter *iter)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	GVariant *value;
	const gchar *key;
	g_autoptr(GVariantIter) iter2 = g_variant_iter_copy (iter);
	g_autoptr(GVariantIter) iter3 = g_variant_iter_copy (iter);

	/* three passes, as we have to construct Id -> Url -> * */
	while (g_variant_iter_loop (iter, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, FWUPD_RESULT_KEY_REMOTE_ID) == 0)
			fwupd_remote_set_id (self, g_variant_get_string (value, NULL));
		if (g_strcmp0 (key, "Type") == 0)
			fwupd_remote_set_kind (self, g_variant_get_uint32 (value));
		if (g_strcmp0 (key, "Keyring") == 0)
			fwupd_remote_set_keyring_kind (self, g_variant_get_uint32 (value));
	}
	while (g_variant_iter_loop (iter2, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, FWUPD_RESULT_KEY_URI) == 0)
			fwupd_remote_set_metadata_uri (self, g_variant_get_string (value, NULL));
		if (g_strcmp0 (key, "FilenameCache") == 0)
			fwupd_remote_set_filename_cache (self, g_variant_get_string (value, NULL));
		if (g_strcmp0 (key, "FilenameSource") == 0)
			fwupd_remote_set_filename_source (self, g_variant_get_string (value, NULL));
		if (g_strcmp0 (key, "ReportUri") == 0)
			fwupd_remote_set_report_uri (self, g_variant_get_string (value, NULL));
	}
	while (g_variant_iter_loop (iter3, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, "Username") == 0) {
			fwupd_remote_set_username (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, "Password") == 0) {
			fwupd_remote_set_password (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, "Title") == 0) {
			fwupd_remote_set_title (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, "Agreement") == 0) {
			fwupd_remote_set_agreement (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, FWUPD_RESULT_KEY_CHECKSUM) == 0) {
			fwupd_remote_set_checksum (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, "Enabled") == 0) {
			priv->enabled = g_variant_get_boolean (value);
		} else if (g_strcmp0 (key, "ApprovalRequired") == 0) {
			priv->approval_required = g_variant_get_boolean (value);
		} else if (g_strcmp0 (key, "Priority") == 0) {
			priv->priority = g_variant_get_int32 (value);
		} else if (g_strcmp0 (key, "ModificationTime") == 0) {
			priv->mtime = g_variant_get_uint64 (value);
		} else if (g_strcmp0 (key, "FirmwareBaseUri") == 0) {
			fwupd_remote_set_firmware_base_uri (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, "AutomaticReports") == 0) {
			priv->automatic_reports = g_variant_get_boolean (value);
		}
	}
}

/**
 * fwupd_remote_to_variant:
 * @self: A #FwupdRemote
 *
 * Creates a GVariant from the remote data.
 *
 * Returns: the GVariant, or %NULL for error
 *
 * Since: 1.0.0
 **/
GVariant *
fwupd_remote_to_variant (FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE (self);
	GVariantBuilder builder;

	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->id != NULL) {
		g_variant_builder_add (&builder, "{sv}", FWUPD_RESULT_KEY_REMOTE_ID,
				       g_variant_new_string (priv->id));
	}
	if (priv->username != NULL) {
		g_variant_builder_add (&builder, "{sv}", "Username",
				       g_variant_new_string (priv->username));
	}
	if (priv->password != NULL) {
		g_variant_builder_add (&builder, "{sv}", "Password",
				       g_variant_new_string (priv->password));
	}
	if (priv->title != NULL) {
		g_variant_builder_add (&builder, "{sv}", "Title",
				       g_variant_new_string (priv->title));
	}
	if (priv->agreement != NULL) {
		g_variant_builder_add (&builder, "{sv}", "Agreement",
				       g_variant_new_string (priv->agreement));
	}
	if (priv->checksum != NULL) {
		g_variant_builder_add (&builder, "{sv}", FWUPD_RESULT_KEY_CHECKSUM,
				       g_variant_new_string (priv->checksum));
	}
	if (priv->metadata_uri != NULL) {
		g_variant_builder_add (&builder, "{sv}", FWUPD_RESULT_KEY_URI,
				       g_variant_new_string (priv->metadata_uri));
	}
	if (priv->report_uri != NULL) {
		g_variant_builder_add (&builder, "{sv}", "ReportUri",
				       g_variant_new_string (priv->report_uri));
	}
	if (priv->firmware_base_uri != NULL) {
		g_variant_builder_add (&builder, "{sv}", "FirmwareBaseUri",
				       g_variant_new_string (priv->firmware_base_uri));
	}
	if (priv->priority != 0) {
		g_variant_builder_add (&builder, "{sv}", "Priority",
				       g_variant_new_int32 (priv->priority));
	}
	if (priv->kind != FWUPD_REMOTE_KIND_UNKNOWN) {
		g_variant_builder_add (&builder, "{sv}", "Type",
				       g_variant_new_uint32 (priv->kind));
	}
	if (priv->keyring_kind != FWUPD_KEYRING_KIND_UNKNOWN) {
		g_variant_builder_add (&builder, "{sv}", "Keyring",
				       g_variant_new_uint32 (priv->keyring_kind));
	}
	if (priv->mtime != 0) {
		g_variant_builder_add (&builder, "{sv}", "ModificationTime",
				       g_variant_new_uint64 (priv->mtime));
	}
	if (priv->filename_cache != NULL) {
		g_variant_builder_add (&builder, "{sv}", "FilenameCache",
				       g_variant_new_string (priv->filename_cache));
	}
	if (priv->filename_source != NULL) {
		g_variant_builder_add (&builder, "{sv}", "FilenameSource",
				       g_variant_new_string (priv->filename_source));
	}
	if (priv->remotes_dir != NULL) {
		g_variant_builder_add (&builder, "{sv}", "RemotesDir",
				       g_variant_new_string (priv->remotes_dir));
	}
	g_variant_builder_add (&builder, "{sv}", "Enabled",
			       g_variant_new_boolean (priv->enabled));
	g_variant_builder_add (&builder, "{sv}", "ApprovalRequired",
			       g_variant_new_boolean (priv->approval_required));
	g_variant_builder_add (&builder, "{sv}", "AutomaticReports",
			       g_variant_new_boolean (priv->automatic_reports));
	return g_variant_new ("a{sv}", &builder);
}

static void
fwupd_remote_get_property (GObject *obj, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdRemote *self = FWUPD_REMOTE (obj);
	FwupdRemotePrivate *priv = GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_ENABLED:
		g_value_set_boolean (value, priv->enabled);
		break;
	case PROP_APPROVAL_REQUIRED:
		g_value_set_boolean (value, priv->approval_required);
		break;
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_AUTOMATIC_REPORTS:
		g_value_set_boolean (value, priv->automatic_reports);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
fwupd_remote_set_property (GObject *obj, guint prop_id,
			   const GValue *value, GParamSpec *pspec)
{
	FwupdRemote *self = FWUPD_REMOTE (obj);
	FwupdRemotePrivate *priv = GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_ENABLED:
		priv->enabled = g_value_get_boolean (value);
		break;
	case PROP_APPROVAL_REQUIRED:
		priv->approval_required = g_value_get_boolean (value);
		break;
	case PROP_ID:
		fwupd_remote_set_id (self, g_value_get_string (value));
		break;
	case PROP_AUTOMATIC_REPORTS:
		priv->automatic_reports = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
fwupd_remote_class_init (FwupdRemoteClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fwupd_remote_finalize;
	object_class->get_property = fwupd_remote_get_property;
	object_class->set_property = fwupd_remote_set_property;

	/**
	 * FwupdRemote:id:
	 *
	 * The remote ID.
	 *
	 * Since: 0.9.3
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * FwupdRemote:enabled:
	 *
	 * If the remote is enabled and should be used.
	 *
	 * Since: 0.9.3
	 */
	pspec = g_param_spec_boolean ("enabled", NULL, NULL,
				      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_ENABLED, pspec);

	/**
	 * FwupdRemote:approval-required:
	 *
	 * If firmware from the remote should be checked against the system
	 * list of approved firmware.
	 *
	 * Since: 1.2.6
	 */
	pspec = g_param_spec_boolean ("approval-required", NULL, NULL,
				      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_APPROVAL_REQUIRED, pspec);

	/**
	* FwupdRemote:automatic-reports:
	*
	* The behavior for auto-uploading reports.
	*
	* Since: 1.3.3
	*/
	pspec = g_param_spec_boolean ("automatic-reports", NULL, NULL,
				      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_AUTOMATIC_REPORTS, pspec);

}

static void
fwupd_remote_init (FwupdRemote *self)
{
}

static void
fwupd_remote_finalize (GObject *obj)
{
	FwupdRemote *self = FWUPD_REMOTE (obj);
	FwupdRemotePrivate *priv = GET_PRIVATE (self);

	g_free (priv->id);
	g_free (priv->metadata_uri);
	g_free (priv->metadata_uri_sig);
	g_free (priv->firmware_base_uri);
	g_free (priv->report_uri);
	g_free (priv->username);
	g_free (priv->password);
	g_free (priv->title);
	g_free (priv->agreement);
	g_free (priv->remotes_dir);
	g_free (priv->checksum);
	g_free (priv->filename_cache);
	g_free (priv->filename_cache_sig);
	g_free (priv->filename_source);
	g_strfreev (priv->order_after);
	g_strfreev (priv->order_before);

	G_OBJECT_CLASS (fwupd_remote_parent_class)->finalize (obj);
}

/**
 * fwupd_remote_from_variant:
 * @value: a #GVariant
 *
 * Creates a new remote using packed data.
 *
 * Returns: (transfer full): a new #FwupdRemote, or %NULL if @value was invalid
 *
 * Since: 1.0.0
 **/
FwupdRemote *
fwupd_remote_from_variant (GVariant *value)
{
	FwupdRemote *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	type_string = g_variant_get_type_string (value);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		rel = fwupd_remote_new ();
		g_variant_get (value, "(a{sv})", &iter);
		fwupd_remote_set_from_variant_iter (rel, iter);
		fwupd_remote_set_from_variant_iter (rel, iter);
	} else if (g_strcmp0 (type_string, "a{sv}") == 0) {
		rel = fwupd_remote_new ();
		g_variant_get (value, "a{sv}", &iter);
		fwupd_remote_set_from_variant_iter (rel, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return rel;
}

/**
 * fwupd_remote_array_from_variant:
 * @value: a #GVariant
 *
 * Creates an array of new devices using packed data.
 *
 * Returns: (transfer container) (element-type FwupdRemote): remotes, or %NULL if @value was invalid
 *
 * Since: 1.2.10
 **/
GPtrArray *
fwupd_remote_array_from_variant (GVariant *value)
{
	GPtrArray *remotes = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	remotes = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	untuple = g_variant_get_child_value (value, 0);
	sz = g_variant_n_children (untuple);
	for (guint i = 0; i < sz; i++) {
		g_autoptr(GVariant) data = g_variant_get_child_value (untuple, i);
		FwupdRemote *remote = fwupd_remote_from_variant (data);
		g_ptr_array_add (remotes, remote);
	}

	return remotes;
}

/**
 * fwupd_remote_new:
 *
 * Creates a new fwupd remote.
 *
 * Returns: a new #FwupdRemote
 *
 * Since: 0.9.3
 **/
FwupdRemote *
fwupd_remote_new (void)
{
	FwupdRemote *self;
	self = g_object_new (FWUPD_TYPE_REMOTE, NULL);
	return FWUPD_REMOTE (self);
}
