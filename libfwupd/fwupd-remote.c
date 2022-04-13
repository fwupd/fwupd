/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif
#include <jcat.h>

#include "fwupd-common-private.h"
#include "fwupd-deprecated.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-remote-private.h"

/**
 * FwupdRemote:
 *
 * A source of metadata that provides firmware.
 *
 * Remotes can be local (e.g. folders on a disk) or remote (e.g. downloaded
 * over HTTP or IPFS).
 *
 * See also: [class@FwupdClient]
 */

static void
fwupd_remote_finalize(GObject *obj);

typedef struct {
	FwupdRemoteKind kind;
	FwupdKeyringKind keyring_kind;
	gchar *id;
	gchar *firmware_base_uri;
	gchar *report_uri;
	gchar *security_report_uri;
	gchar *metadata_uri;
	gchar *metadata_uri_sig;
	gchar *username;
	gchar *password;
	gchar *title;
	gchar *agreement;
	gchar *checksum;
	gchar *filename_cache;
	gchar *filename_cache_sig;
	gchar *filename_source;
	gboolean enabled;
	gboolean approval_required;
	gint priority;
	guint64 mtime;
	gchar **order_after;
	gchar **order_before;
	gchar *remotes_dir;
	gboolean automatic_reports;
	gboolean automatic_security_reports;
} FwupdRemotePrivate;

enum {
	PROP_0,
	PROP_ID,
	PROP_ENABLED,
	PROP_APPROVAL_REQUIRED,
	PROP_AUTOMATIC_REPORTS,
	PROP_AUTOMATIC_SECURITY_REPORTS,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE(FwupdRemote, fwupd_remote, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_remote_get_instance_private(o))

#ifdef HAVE_LIBCURL_7_62_0
typedef gchar curlptr;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(curlptr, curl_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)
#endif

/**
 * fwupd_remote_to_json:
 * @self: a #FwupdRemote
 * @builder: a JSON builder
 *
 * Adds a fwupd remote to a JSON builder
 *
 * Since: 1.6.2
 **/
void
fwupd_remote_to_json(FwupdRemote *self, JsonBuilder *builder)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_REMOTE(self));
	g_return_if_fail(builder != NULL);

	fwupd_common_json_add_string(builder, "Id", priv->id);
	if (priv->kind != FWUPD_REMOTE_KIND_UNKNOWN) {
		fwupd_common_json_add_string(builder,
					     "Kind",
					     fwupd_remote_kind_to_string(priv->kind));
	}
	if (priv->keyring_kind != FWUPD_KEYRING_KIND_UNKNOWN) {
		fwupd_common_json_add_string(builder,
					     "KeyringKind",
					     fwupd_keyring_kind_to_string(priv->keyring_kind));
	}
	fwupd_common_json_add_string(builder, "FirmwareBaseUri", priv->firmware_base_uri);
	fwupd_common_json_add_string(builder, "ReportUri", priv->report_uri);
	fwupd_common_json_add_string(builder, "SecurityReportUri", priv->security_report_uri);
	fwupd_common_json_add_string(builder, "MetadataUri", priv->metadata_uri);
	fwupd_common_json_add_string(builder, "MetadataUriSig", priv->metadata_uri_sig);
	fwupd_common_json_add_string(builder, "Username", priv->username);
	fwupd_common_json_add_string(builder, "Password", priv->password);
	fwupd_common_json_add_string(builder, "Title", priv->title);
	fwupd_common_json_add_string(builder, "Agreement", priv->agreement);
	fwupd_common_json_add_string(builder, "Checksum", priv->checksum);
	fwupd_common_json_add_string(builder, "FilenameCache", priv->filename_cache);
	fwupd_common_json_add_string(builder, "FilenameCacheSig", priv->filename_cache_sig);
	fwupd_common_json_add_string(builder, "FilenameSource", priv->filename_source);
	fwupd_common_json_add_boolean(builder, "Enabled", priv->enabled);
	fwupd_common_json_add_boolean(builder, "ApprovalRequired", priv->approval_required);
	fwupd_common_json_add_boolean(builder, "AutomaticReports", priv->automatic_reports);
	fwupd_common_json_add_boolean(builder,
				      "AutomaticSecurityReports",
				      priv->automatic_security_reports);
	fwupd_common_json_add_int(builder, "Priority", priv->priority);
	fwupd_common_json_add_int(builder, "Mtime", priv->mtime);
	fwupd_common_json_add_string(builder, "RemotesDir", priv->remotes_dir);
	fwupd_common_json_add_stringv(builder, "OrderAfter", priv->order_after);
	fwupd_common_json_add_stringv(builder, "OrderBefore", priv->order_before);
}

static gchar *
fwupd_strdup_nonempty(const gchar *text)
{
	if (text == NULL || text[0] == '\0')
		return NULL;
	return g_strdup(text);
}

static void
fwupd_remote_set_username(FwupdRemote *self, const gchar *username)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->username, username) == 0)
		return;

	g_free(priv->username);
	priv->username = g_strdup(username);
}

static void
fwupd_remote_set_title(FwupdRemote *self, const gchar *title)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->title, title) == 0)
		return;

	g_free(priv->title);
	priv->title = g_strdup(title);
}

/**
 * fwupd_remote_set_agreement:
 * @self: a #FwupdRemote
 * @agreement: (nullable): agreement markup text
 *
 * Sets the remote agreement in AppStream markup format
 *
 * Since: 1.0.7
 **/
void
fwupd_remote_set_agreement(FwupdRemote *self, const gchar *agreement)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->agreement, agreement) == 0)
		return;

	g_free(priv->agreement);
	priv->agreement = g_strdup(agreement);
}

static void
fwupd_remote_set_checksum(FwupdRemote *self, const gchar *checksum)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->checksum, checksum) == 0)
		return;

	g_free(priv->checksum);
	priv->checksum = g_strdup(checksum);
}

static void
fwupd_remote_set_password(FwupdRemote *self, const gchar *password)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->password, password) == 0)
		return;

	g_free(priv->password);
	priv->password = g_strdup(password);
}

static void
fwupd_remote_set_kind(FwupdRemote *self, FwupdRemoteKind kind)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	priv->kind = kind;
}

/**
 * fwupd_remote_set_keyring_kind:
 * @self: a #FwupdRemote
 * @keyring_kind: keyring kind e.g. #FWUPD_KEYRING_KIND_PKCS7
 *
 * Sets the keyring kind
 *
 * Since: 1.5.3
 **/
void
fwupd_remote_set_keyring_kind(FwupdRemote *self, FwupdKeyringKind keyring_kind)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	priv->keyring_kind = keyring_kind;
}

/* note, this has to be set before url */
static void
fwupd_remote_set_id(FwupdRemote *self, const gchar *id)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->id, id) == 0)
		return;

	g_free(priv->id);
	priv->id = g_strdup(id);
	g_strdelimit(priv->id, ".", '\0');
}

/**
 * fwupd_remote_set_filename_source:
 * @self: a #FwupdRemote
 * @filename_source: (nullable): filename
 *
 * Sets the source filename. This is typically a file in `/etc/fwupd/remotes/`.
 *
 * Since: 1.6.1
 **/
void
fwupd_remote_set_filename_source(FwupdRemote *self, const gchar *filename_source)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	if (priv->filename_source == filename_source)
		return;
	g_free(priv->filename_source);
	priv->filename_source = g_strdup(filename_source);
}

static const gchar *
fwupd_remote_get_suffix_for_keyring_kind(FwupdKeyringKind keyring_kind)
{
	if (keyring_kind == FWUPD_KEYRING_KIND_JCAT)
		return ".jcat";
	if (keyring_kind == FWUPD_KEYRING_KIND_GPG)
		return ".asc";
	if (keyring_kind == FWUPD_KEYRING_KIND_PKCS7)
		return ".p7b";
	return NULL;
}

static gchar *
fwupd_remote_build_uri(FwupdRemote *self, const gchar *url, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
#ifdef HAVE_LIBCURL_7_62_0
	g_autoptr(curlptr) tmp_uri = NULL;
	g_autoptr(CURLU) uri = curl_url();

	/* create URI, substituting if required */
	if (priv->firmware_base_uri != NULL) {
		g_autofree gchar *basename = NULL;
		g_autofree gchar *path_new = NULL;
		g_autoptr(curlptr) path = NULL;
		g_autoptr(CURLU) uri_tmp = curl_url();
		if (curl_url_set(uri_tmp, CURLUPART_URL, url, 0) != CURLUE_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Failed to parse url '%s'",
				    url);
			return NULL;
		}
		(void)curl_url_get(uri_tmp, CURLUPART_PATH, &path, 0);
		basename = g_path_get_basename(path);
		path_new = g_build_filename(priv->firmware_base_uri, basename, NULL);
		(void)curl_url_set(uri, CURLUPART_URL, path_new, 0);

		/* use the base URI of the metadata to build the full path */
	} else if (g_strstr_len(url, -1, "/") == NULL) {
		g_autofree gchar *basename = NULL;
		g_autofree gchar *path_new = NULL;
		g_autoptr(curlptr) path = NULL;
		if (curl_url_set(uri, CURLUPART_URL, priv->metadata_uri, 0) != CURLUE_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Failed to parse url '%s'",
				    priv->metadata_uri);
			return NULL;
		}
		(void)curl_url_get(uri, CURLUPART_PATH, &path, 0);
		basename = g_path_get_dirname(path);
		path_new = g_build_filename(basename, url, NULL);
		(void)curl_url_set(uri, CURLUPART_URL, path_new, 0);

		/* a normal URI */
	} else {
		if (curl_url_set(uri, CURLUPART_URL, url, 0) != CURLUE_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Failed to parse URI '%s'",
				    url);
			return NULL;
		}
	}

	/* set the username and password */
	if (priv->username != NULL)
		(void)curl_url_set(uri, CURLUPART_USER, priv->username, 0);
	if (priv->password != NULL)
		(void)curl_url_set(uri, CURLUPART_PASSWORD, priv->password, 0);
	(void)curl_url_get(uri, CURLUPART_URL, &tmp_uri, 0);
	return g_strdup(tmp_uri);
#else
	if (priv->firmware_base_uri != NULL) {
		g_autofree gchar *basename = g_path_get_basename(url);
		return g_build_filename(priv->firmware_base_uri, basename, NULL);
	}
	if (g_strstr_len(url, -1, "/") == NULL) {
		g_autofree gchar *basename = g_path_get_dirname(priv->metadata_uri);
		return g_build_filename(basename, url, NULL);
	}
	return g_strdup(url);
#endif
}

/* note, this has to be set before username and password */
static void
fwupd_remote_set_metadata_uri(FwupdRemote *self, const gchar *metadata_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	const gchar *suffix;

	/* save this so we can export the object as a GVariant */
	priv->metadata_uri = g_strdup(metadata_uri);

	/* generate the signature URI too */
	suffix = fwupd_remote_get_suffix_for_keyring_kind(priv->keyring_kind);
	if (suffix != NULL)
		priv->metadata_uri_sig = g_strconcat(metadata_uri, suffix, NULL);
}

/* note, this has to be set after MetadataURI */
static void
fwupd_remote_set_firmware_base_uri(FwupdRemote *self, const gchar *firmware_base_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->firmware_base_uri, firmware_base_uri) == 0)
		return;

	g_free(priv->firmware_base_uri);
	priv->firmware_base_uri = g_strdup(firmware_base_uri);
}

static void
fwupd_remote_set_report_uri(FwupdRemote *self, const gchar *report_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *report_uri_safe = fwupd_strdup_nonempty(report_uri);

	/* not changed */
	if (g_strcmp0(priv->report_uri, report_uri_safe) == 0)
		return;

	g_free(priv->report_uri);
	priv->report_uri = g_steal_pointer(&report_uri_safe);
}

static void
fwupd_remote_set_security_report_uri(FwupdRemote *self, const gchar *security_report_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *security_report_uri_safe = fwupd_strdup_nonempty(security_report_uri);

	/* not changed */
	if (g_strcmp0(priv->security_report_uri, security_report_uri_safe) == 0)
		return;

	g_free(priv->security_report_uri);
	priv->security_report_uri = g_steal_pointer(&security_report_uri_safe);
}

/**
 * fwupd_remote_kind_from_string:
 * @kind: (nullable): a string, e.g. `download`
 *
 * Converts an printable string to an enumerated type.
 *
 * Returns: a #FwupdRemoteKind, e.g. %FWUPD_REMOTE_KIND_DOWNLOAD
 *
 * Since: 0.9.6
 **/
FwupdRemoteKind
fwupd_remote_kind_from_string(const gchar *kind)
{
	if (g_strcmp0(kind, "download") == 0)
		return FWUPD_REMOTE_KIND_DOWNLOAD;
	if (g_strcmp0(kind, "local") == 0)
		return FWUPD_REMOTE_KIND_LOCAL;
	if (g_strcmp0(kind, "directory") == 0)
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
fwupd_remote_kind_to_string(FwupdRemoteKind kind)
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
fwupd_remote_set_filename_cache(FwupdRemote *self, const gchar *filename)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	const gchar *suffix;

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->filename_cache, filename) == 0)
		return;

	g_free(priv->filename_cache);
	priv->filename_cache = g_strdup(filename);

	/* create for all remote types */
	suffix = fwupd_remote_get_suffix_for_keyring_kind(priv->keyring_kind);
	if (suffix != NULL) {
		g_free(priv->filename_cache_sig);
		priv->filename_cache_sig = g_strconcat(filename, suffix, NULL);
	}
}

static void
fwupd_remote_set_order_before(FwupdRemote *self, const gchar *order_before)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_clear_pointer(&priv->order_before, g_strfreev);
	if (order_before != NULL)
		priv->order_before = g_strsplit_set(order_before, ",:;", -1);
}

static void
fwupd_remote_set_order_after(FwupdRemote *self, const gchar *order_after)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_clear_pointer(&priv->order_after, g_strfreev);
	if (order_after != NULL)
		priv->order_after = g_strsplit_set(order_after, ",:;", -1);
}

/**
 * fwupd_remote_setup:
 * @self: a #FwupdRemote
 * @error: (nullable): optional return location for an error
 *
 * Sets up the remote ready for use, checking that required parameters have
 * been set. Calling this method multiple times has no effect.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.1
 **/
gboolean
fwupd_remote_setup(FwupdRemote *self, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* we can override, hence the extra section */
	if (priv->kind == FWUPD_REMOTE_KIND_UNKNOWN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "metadata kind invalid");
		return FALSE;
	}

	/* some validation for DOWNLOAD types */
	if (priv->kind == FWUPD_REMOTE_KIND_DOWNLOAD) {
		g_autofree gchar *filename_cache = NULL;

		if (priv->remotes_dir == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "remotes directory not set");
			return FALSE;
		}
		/* set cache to /var/lib... */
		filename_cache =
		    g_build_filename(priv->remotes_dir, priv->id, "metadata.xml.gz", NULL);
		fwupd_remote_set_filename_cache(self, filename_cache);
	}

	/* some validation for DIRECTORY types */
	if (priv->kind == FWUPD_REMOTE_KIND_DIRECTORY) {
		if (priv->keyring_kind != FWUPD_KEYRING_KIND_NONE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "keyring kind %s is not supported with directory remote",
				    fwupd_keyring_kind_to_string(priv->keyring_kind));
			return FALSE;
		}
		if (priv->firmware_base_uri != NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "Directory remotes don't support firmware base URI");
			return FALSE;
		}
	}

	/* load the checksum */
	if (priv->filename_cache_sig != NULL &&
	    g_file_test(priv->filename_cache_sig, G_FILE_TEST_EXISTS)) {
		gsize sz = 0;
		g_autofree gchar *buf = NULL;
		g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
		if (!g_file_get_contents(priv->filename_cache_sig, &buf, &sz, error)) {
			g_prefix_error(error, "failed to get checksum: ");
			return FALSE;
		}
		g_checksum_update(checksum, (guchar *)buf, (gssize)sz);
		fwupd_remote_set_checksum(self, g_checksum_get_string(checksum));
	} else {
		fwupd_remote_set_checksum(self, NULL);
	}

	/* success */
	return TRUE;
}

/**
 * fwupd_remote_load_from_filename:
 * @self: a #FwupdRemote
 * @filename: (not nullable): a filename
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Loads metadata about the remote from a keyfile.
 * This can be called zero or multiple times for each remote.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.9.3
 **/
gboolean
fwupd_remote_load_from_filename(FwupdRemote *self,
				const gchar *filename,
				GCancellable *cancellable,
				GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
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

	/* optional verification type */
	if (g_key_file_has_key(kf, group, "Keyring", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "Keyring", NULL);
		priv->keyring_kind = fwupd_keyring_kind_from_string(tmp);
		if (priv->keyring_kind == FWUPD_KEYRING_KIND_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "keyring kind '%s' unknown",
				    tmp);
			return FALSE;
		}
	}

	/* the first remote sets the URI, even if it's file:// to the cache */
	if (g_key_file_has_key(kf, group, "MetadataURI", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "MetadataURI", NULL);
		if (g_str_has_prefix(tmp, "file://")) {
			const gchar *filename_cache = tmp;
			if (g_str_has_prefix(filename_cache, "file://"))
				filename_cache += 7;
			fwupd_remote_set_filename_cache(self, filename_cache);
			if (g_file_test(filename_cache, G_FILE_TEST_IS_DIR))
				priv->kind = FWUPD_REMOTE_KIND_DIRECTORY;
			else
				priv->kind = FWUPD_REMOTE_KIND_LOCAL;
		} else if (g_str_has_prefix(tmp, "http://") || g_str_has_prefix(tmp, "https://") ||
			   g_str_has_prefix(tmp, "ipfs://") || g_str_has_prefix(tmp, "ipns://")) {
			priv->kind = FWUPD_REMOTE_KIND_DOWNLOAD;
			fwupd_remote_set_metadata_uri(self, tmp);
		}
	}

	/* all keys are optional */
	if (g_key_file_has_key(kf, group, "Enabled", NULL))
		priv->enabled = g_key_file_get_boolean(kf, group, "Enabled", NULL);
	if (g_key_file_has_key(kf, group, "ApprovalRequired", NULL))
		priv->approval_required =
		    g_key_file_get_boolean(kf, group, "ApprovalRequired", NULL);
	if (g_key_file_has_key(kf, group, "Title", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "Title", NULL);
		fwupd_remote_set_title(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "ReportURI", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "ReportURI", NULL);
		fwupd_remote_set_report_uri(self, tmp);
	}
	if (g_key_file_has_key(kf, group, "SecurityReportURI", NULL)) {
		g_autofree gchar *tmp = g_key_file_get_string(kf, group, "SecurityReportURI", NULL);
		fwupd_remote_set_security_report_uri(self, tmp);
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
	if (g_key_file_has_key(kf, group, "AutomaticReports", NULL))
		priv->automatic_reports =
		    g_key_file_get_boolean(kf, group, "AutomaticReports", NULL);
	if (g_key_file_has_key(kf, group, "AutomaticSecurityReports", NULL))
		priv->automatic_security_reports =
		    g_key_file_get_boolean(kf, group, "AutomaticSecurityReports", NULL);

	/* success */
	fwupd_remote_set_filename_source(self, filename);
	return TRUE;
}

/**
 * fwupd_remote_get_order_after:
 * @self: a #FwupdRemote
 *
 * Gets the list of remotes this plugin should be ordered after.
 *
 * Returns: (transfer none): an array
 *
 * Since: 0.9.5
 **/
gchar **
fwupd_remote_get_order_after(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->order_after;
}

/**
 * fwupd_remote_get_order_before:
 * @self: a #FwupdRemote
 *
 * Gets the list of remotes this plugin should be ordered before.
 *
 * Returns: (transfer none): an array
 *
 * Since: 0.9.5
 **/
gchar **
fwupd_remote_get_order_before(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->order_before;
}

/**
 * fwupd_remote_get_filename_cache:
 * @self: a #FwupdRemote
 *
 * Gets the path and filename that the remote is using for a cache.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.6
 **/
const gchar *
fwupd_remote_get_filename_cache(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->filename_cache;
}

/**
 * fwupd_remote_get_filename_cache_sig:
 * @self: a #FwupdRemote
 *
 * Gets the path and filename that the remote is using for a signature cache.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_filename_cache_sig(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->filename_cache_sig;
}

/**
 * fwupd_remote_get_filename_source:
 * @self: a #FwupdRemote
 *
 * Gets the path and filename of the remote itself, typically a `.conf` file.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.8
 **/
const gchar *
fwupd_remote_get_filename_source(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->filename_source;
}

/**
 * fwupd_remote_get_priority:
 * @self: a #FwupdRemote
 *
 * Gets the priority of the remote, where bigger numbers are better.
 *
 * Returns: a priority, or 0 for the default value
 *
 * Since: 0.9.5
 **/
gint
fwupd_remote_get_priority(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), 0);
	return priv->priority;
}

/**
 * fwupd_remote_get_kind:
 * @self: a #FwupdRemote
 *
 * Gets the kind of the remote.
 *
 * Returns: a #FwupdRemoteKind, e.g. #FWUPD_REMOTE_KIND_LOCAL
 *
 * Since: 0.9.6
 **/
FwupdRemoteKind
fwupd_remote_get_kind(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), 0);
	return priv->kind;
}

/**
 * fwupd_remote_get_keyring_kind:
 * @self: a #FwupdRemote
 *
 * Gets the keyring kind of the remote.
 *
 * Returns: a #FwupdKeyringKind, e.g. #FWUPD_KEYRING_KIND_GPG
 *
 * Since: 0.9.7
 **/
FwupdKeyringKind
fwupd_remote_get_keyring_kind(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), 0);
	return priv->keyring_kind;
}

/**
 * fwupd_remote_get_age:
 * @self: a #FwupdRemote
 *
 * Gets the age of the remote in seconds.
 *
 * Returns: a age, or %G_MAXUINT64 for unavailable
 *
 * Since: 0.9.5
 **/
guint64
fwupd_remote_get_age(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	guint64 now;
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), 0);
	now = (guint64)g_get_real_time() / G_USEC_PER_SEC;
	if (priv->mtime > now)
		return G_MAXUINT64;
	return now - priv->mtime;
}

/**
 * fwupd_remote_set_remotes_dir:
 * @self: a #FwupdRemote
 * @directory: (nullable): Remotes directory
 *
 * Sets the directory to store remote data
 *
 * Since: 1.3.1
 **/
void
fwupd_remote_set_remotes_dir(FwupdRemote *self, const gchar *directory)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->remotes_dir, directory) == 0)
		return;

	g_free(priv->remotes_dir);
	priv->remotes_dir = g_strdup(directory);
}

/**
 * fwupd_remote_set_priority:
 * @self: a #FwupdRemote
 * @priority: an integer, where 1 is better
 *
 * Sets the plugin priority.
 *
 * Since: 0.9.5
 **/
void
fwupd_remote_set_priority(FwupdRemote *self, gint priority)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	priv->priority = priority;
}

/**
 * fwupd_remote_set_mtime:
 * @self: a #FwupdRemote
 * @mtime: a UNIX timestamp
 *
 * Sets the plugin modification time.
 *
 * Since: 0.9.5
 **/
void
fwupd_remote_set_mtime(FwupdRemote *self, guint64 mtime)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	priv->mtime = mtime;
}

/**
 * fwupd_remote_get_username:
 * @self: a #FwupdRemote
 *
 * Gets the username configured for the remote.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.5
 **/
const gchar *
fwupd_remote_get_username(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->username;
}

/**
 * fwupd_remote_get_password:
 * @self: a #FwupdRemote
 *
 * Gets the password configured for the remote.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 0.9.5
 **/
const gchar *
fwupd_remote_get_password(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->password;
}

/**
 * fwupd_remote_get_title:
 * @self: a #FwupdRemote
 *
 * Gets the remote title, e.g. `Linux Vendor Firmware Service`.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 0.9.8
 **/
const gchar *
fwupd_remote_get_title(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->title;
}

/**
 * fwupd_remote_get_agreement:
 * @self: a #FwupdRemote
 *
 * Gets the remote agreement in AppStream markup format
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.0.7
 **/
const gchar *
fwupd_remote_get_agreement(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->agreement;
}

/**
 * fwupd_remote_get_remotes_dir:
 * @self: a #FwupdRemote
 *
 * Gets the base directory for storing remote metadata
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.3.1
 **/
const gchar *
fwupd_remote_get_remotes_dir(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->remotes_dir;
}

/**
 * fwupd_remote_get_checksum:
 * @self: a #FwupdRemote
 *
 * Gets the remote checksum.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.0.0
 **/
const gchar *
fwupd_remote_get_checksum(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->checksum;
}

/**
 * fwupd_remote_build_firmware_uri:
 * @self: a #FwupdRemote
 * @url: (not nullable): the URL to use
 * @error: (nullable): optional return location for an error
 *
 * Builds a URI for the URL using the username and password set for the remote,
 * including any basename URI substitution.
 *
 * Returns: (transfer full): a URI, or %NULL for error
 *
 * Since: 0.9.7
 **/
gchar *
fwupd_remote_build_firmware_uri(FwupdRemote *self, const gchar *url, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	g_return_val_if_fail(url != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fwupd_remote_build_uri(self, url, error);
}

/**
 * fwupd_remote_get_report_uri:
 * @self: a #FwupdRemote
 *
 * Gets the URI for the remote reporting.
 *
 * Returns: (transfer none): a URI, or %NULL for invalid.
 *
 * Since: 1.0.4
 **/
const gchar *
fwupd_remote_get_report_uri(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->report_uri;
}

/**
 * fwupd_remote_get_security_report_uri:
 * @self: a #FwupdRemote
 *
 * Gets the URI for the security report.
 *
 * Returns: (transfer none): a URI, or %NULL for invalid.
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_remote_get_security_report_uri(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->security_report_uri;
}

/**
 * fwupd_remote_get_metadata_uri:
 * @self: a #FwupdRemote
 *
 * Gets the URI for the remote metadata.
 *
 * Returns: (transfer none): a URI, or %NULL for invalid.
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_metadata_uri(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->metadata_uri;
}

static gboolean
fwupd_remote_load_signature_jcat(FwupdRemote *self, JcatFile *jcat_file, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	const gchar *id;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *baseuri = NULL;
	g_autofree gchar *metadata_uri = NULL;
	g_autoptr(JcatItem) jcat_item = NULL;

	/* this seems pointless to get the item by ID then just read the ID,
	 * but _get_item_by_id() uses the AliasIds as a fallback */
	basename = g_path_get_basename(priv->metadata_uri);
	jcat_item = jcat_file_get_item_by_id(jcat_file, basename, NULL);
	if (jcat_item == NULL) {
		/* if we're using libjcat 0.1.0 just get the default item */
		jcat_item = jcat_file_get_item_default(jcat_file, error);
		if (jcat_item == NULL)
			return FALSE;
	}
	id = jcat_item_get_id(jcat_item);
	if (id == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "No ID for JCat item");
		return FALSE;
	}

	/* replace the URI if required */
	baseuri = g_path_get_dirname(priv->metadata_uri);
	metadata_uri = g_build_path("/", baseuri, id, NULL);
	if (g_strcmp0(metadata_uri, priv->metadata_uri) != 0) {
		g_debug("changing metadata URI from %s to %s", priv->metadata_uri, metadata_uri);
		g_free(priv->metadata_uri);
		priv->metadata_uri = g_steal_pointer(&metadata_uri);
	}

	/* success */
	return TRUE;
}

/**
 * fwupd_remote_load_signature_bytes:
 * @self: a #FwupdRemote
 * @bytes: (not nullable): data blob
 * @error: (nullable): optional return location for an error
 *
 * Parses the signature, updating the metadata URI as appropriate.
 *
 * This can only be called for remotes with `Keyring=jcat` which is
 * the default for most remotes.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_remote_load_signature_bytes(FwupdRemote *self, GBytes *bytes, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GInputStream) istr = NULL;
	g_autoptr(JcatFile) jcat_file = jcat_file_new();

	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (priv->keyring_kind != FWUPD_KEYRING_KIND_JCAT) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only supported for JCat remotes");
		return FALSE;
	}

	istr = g_memory_input_stream_new_from_bytes(bytes);
	if (!jcat_file_import_stream(jcat_file, istr, JCAT_IMPORT_FLAG_NONE, NULL, error))
		return FALSE;
	return fwupd_remote_load_signature_jcat(self, jcat_file, error);
}

/**
 * fwupd_remote_load_signature:
 * @self: a #FwupdRemote
 * @filename: (not nullable): a filename
 * @error: (nullable): optional return location for an error
 *
 * Parses the signature, updating the metadata URI as appropriate.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.0
 **/
gboolean
fwupd_remote_load_signature(FwupdRemote *self, const gchar *filename, GError **error)
{
	g_autoptr(GFile) gfile = NULL;
	g_autoptr(JcatFile) jcat_file = jcat_file_new();

	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* load JCat file */
	gfile = g_file_new_for_path(filename);
	if (!jcat_file_import_file(jcat_file, gfile, JCAT_IMPORT_FLAG_NONE, NULL, error))
		return FALSE;
	return fwupd_remote_load_signature_jcat(self, jcat_file, error);
}

/**
 * fwupd_remote_get_metadata_uri_sig:
 * @self: a #FwupdRemote
 *
 * Gets the URI for the remote metadata signature.
 *
 * Returns: (transfer none): a URI, or %NULL for invalid.
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_metadata_uri_sig(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->metadata_uri_sig;
}

/**
 * fwupd_remote_get_firmware_base_uri:
 * @self: a #FwupdRemote
 *
 * Gets the base URI for firmware.
 *
 * Returns: (transfer none): a URI, or %NULL for unset.
 *
 * Since: 0.9.7
 **/
const gchar *
fwupd_remote_get_firmware_base_uri(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->firmware_base_uri;
}

/**
 * fwupd_remote_get_enabled:
 * @self: a #FwupdRemote
 *
 * Gets if the remote is enabled and should be used.
 *
 * Returns: a #TRUE if the remote is enabled
 *
 * Since: 0.9.3
 **/
gboolean
fwupd_remote_get_enabled(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	return priv->enabled;
}

/**
 * fwupd_remote_get_automatic_reports:
 * @self: a #FwupdRemote
 *
 * Gets if reports should be automatically uploaded to this remote
 *
 * Returns: a #TRUE if the remote should have reports uploaded automatically
 *
 * Since: 1.3.3
 **/
gboolean
fwupd_remote_get_automatic_reports(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	return priv->automatic_reports;
}

/**
 * fwupd_remote_get_automatic_security_reports:
 * @self: a #FwupdRemote
 *
 * Gets if security reports should be automatically uploaded to this remote
 *
 * Returns: a #TRUE if the remote should have reports uploaded automatically
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_remote_get_automatic_security_reports(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	return priv->automatic_security_reports;
}

/**
 * fwupd_remote_get_approval_required:
 * @self: a #FwupdRemote
 *
 * Gets if firmware from the remote should be checked against the list
 * of a approved checksums.
 *
 * Returns: a #TRUE if the remote is restricted
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_remote_get_approval_required(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	return priv->approval_required;
}

/**
 * fwupd_remote_get_id:
 * @self: a #FwupdRemote
 *
 * Gets the remote ID, e.g. `lvfs-testing`.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_remote_get_id(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->id;
}

static void
fwupd_remote_set_from_variant_iter(FwupdRemote *self, GVariantIter *iter)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	GVariant *value;
	const gchar *key;
	g_autoptr(GVariantIter) iter2 = g_variant_iter_copy(iter);
	g_autoptr(GVariantIter) iter3 = g_variant_iter_copy(iter);

	/* three passes, as we have to construct Id -> Url -> * */
	while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
		if (g_strcmp0(key, FWUPD_RESULT_KEY_REMOTE_ID) == 0)
			fwupd_remote_set_id(self, g_variant_get_string(value, NULL));
		if (g_strcmp0(key, "Type") == 0)
			fwupd_remote_set_kind(self, g_variant_get_uint32(value));
		if (g_strcmp0(key, "Keyring") == 0)
			fwupd_remote_set_keyring_kind(self, g_variant_get_uint32(value));
	}
	while (g_variant_iter_loop(iter2, "{&sv}", &key, &value)) {
		if (g_strcmp0(key, FWUPD_RESULT_KEY_URI) == 0)
			fwupd_remote_set_metadata_uri(self, g_variant_get_string(value, NULL));
		if (g_strcmp0(key, "FilenameCache") == 0)
			fwupd_remote_set_filename_cache(self, g_variant_get_string(value, NULL));
		if (g_strcmp0(key, "FilenameSource") == 0)
			fwupd_remote_set_filename_source(self, g_variant_get_string(value, NULL));
		if (g_strcmp0(key, "ReportUri") == 0)
			fwupd_remote_set_report_uri(self, g_variant_get_string(value, NULL));
		if (g_strcmp0(key, "SecurityReportUri") == 0)
			fwupd_remote_set_security_report_uri(self,
							     g_variant_get_string(value, NULL));
	}
	while (g_variant_iter_loop(iter3, "{&sv}", &key, &value)) {
		if (g_strcmp0(key, "Username") == 0) {
			fwupd_remote_set_username(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Password") == 0) {
			fwupd_remote_set_password(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Title") == 0) {
			fwupd_remote_set_title(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Agreement") == 0) {
			fwupd_remote_set_agreement(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, FWUPD_RESULT_KEY_CHECKSUM) == 0) {
			fwupd_remote_set_checksum(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Enabled") == 0) {
			priv->enabled = g_variant_get_boolean(value);
		} else if (g_strcmp0(key, "ApprovalRequired") == 0) {
			priv->approval_required = g_variant_get_boolean(value);
		} else if (g_strcmp0(key, "Priority") == 0) {
			priv->priority = g_variant_get_int32(value);
		} else if (g_strcmp0(key, "ModificationTime") == 0) {
			priv->mtime = g_variant_get_uint64(value);
		} else if (g_strcmp0(key, "FirmwareBaseUri") == 0) {
			fwupd_remote_set_firmware_base_uri(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "AutomaticReports") == 0) {
			priv->automatic_reports = g_variant_get_boolean(value);
		} else if (g_strcmp0(key, "AutomaticSecurityReports") == 0) {
			priv->automatic_security_reports = g_variant_get_boolean(value);
		}
	}
}

/**
 * fwupd_remote_to_variant:
 * @self: a #FwupdRemote
 *
 * Serialize the remote data.
 *
 * Returns: the serialized data, or %NULL for error
 *
 * Since: 1.0.0
 **/
GVariant *
fwupd_remote_to_variant(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;

	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_REMOTE_ID,
				      g_variant_new_string(priv->id));
	}
	if (priv->username != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "Username",
				      g_variant_new_string(priv->username));
	}
	if (priv->password != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "Password",
				      g_variant_new_string(priv->password));
	}
	if (priv->title != NULL) {
		g_variant_builder_add(&builder, "{sv}", "Title", g_variant_new_string(priv->title));
	}
	if (priv->agreement != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "Agreement",
				      g_variant_new_string(priv->agreement));
	}
	if (priv->checksum != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CHECKSUM,
				      g_variant_new_string(priv->checksum));
	}
	if (priv->metadata_uri != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_URI,
				      g_variant_new_string(priv->metadata_uri));
	}
	if (priv->report_uri != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "ReportUri",
				      g_variant_new_string(priv->report_uri));
	}
	if (priv->security_report_uri != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "SecurityReportUri",
				      g_variant_new_string(priv->security_report_uri));
	}
	if (priv->firmware_base_uri != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "FirmwareBaseUri",
				      g_variant_new_string(priv->firmware_base_uri));
	}
	if (priv->priority != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "Priority",
				      g_variant_new_int32(priv->priority));
	}
	if (priv->kind != FWUPD_REMOTE_KIND_UNKNOWN) {
		g_variant_builder_add(&builder, "{sv}", "Type", g_variant_new_uint32(priv->kind));
	}
	if (priv->keyring_kind != FWUPD_KEYRING_KIND_UNKNOWN) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "Keyring",
				      g_variant_new_uint32(priv->keyring_kind));
	}
	if (priv->mtime != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "ModificationTime",
				      g_variant_new_uint64(priv->mtime));
	}
	if (priv->filename_cache != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "FilenameCache",
				      g_variant_new_string(priv->filename_cache));
	}
	if (priv->filename_source != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "FilenameSource",
				      g_variant_new_string(priv->filename_source));
	}
	if (priv->remotes_dir != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "RemotesDir",
				      g_variant_new_string(priv->remotes_dir));
	}
	g_variant_builder_add(&builder, "{sv}", "Enabled", g_variant_new_boolean(priv->enabled));
	g_variant_builder_add(&builder,
			      "{sv}",
			      "ApprovalRequired",
			      g_variant_new_boolean(priv->approval_required));
	g_variant_builder_add(&builder,
			      "{sv}",
			      "AutomaticReports",
			      g_variant_new_boolean(priv->automatic_reports));
	g_variant_builder_add(&builder,
			      "{sv}",
			      "AutomaticSecurityReports",
			      g_variant_new_boolean(priv->automatic_security_reports));
	return g_variant_new("a{sv}", &builder);
}

static void
fwupd_remote_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdRemote *self = FWUPD_REMOTE(obj);
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	switch (prop_id) {
	case PROP_ENABLED:
		g_value_set_boolean(value, priv->enabled);
		break;
	case PROP_APPROVAL_REQUIRED:
		g_value_set_boolean(value, priv->approval_required);
		break;
	case PROP_ID:
		g_value_set_string(value, priv->id);
		break;
	case PROP_AUTOMATIC_REPORTS:
		g_value_set_boolean(value, priv->automatic_reports);
		break;
	case PROP_AUTOMATIC_SECURITY_REPORTS:
		g_value_set_boolean(value, priv->automatic_security_reports);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fwupd_remote_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FwupdRemote *self = FWUPD_REMOTE(obj);
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	switch (prop_id) {
	case PROP_ENABLED:
		priv->enabled = g_value_get_boolean(value);
		break;
	case PROP_APPROVAL_REQUIRED:
		priv->approval_required = g_value_get_boolean(value);
		break;
	case PROP_ID:
		fwupd_remote_set_id(self, g_value_get_string(value));
		break;
	case PROP_AUTOMATIC_REPORTS:
		priv->automatic_reports = g_value_get_boolean(value);
		break;
	case PROP_AUTOMATIC_SECURITY_REPORTS:
		priv->automatic_security_reports = g_value_get_boolean(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fwupd_remote_class_init(FwupdRemoteClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
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
	pspec =
	    g_param_spec_string("id", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ID, pspec);

	/**
	 * FwupdRemote:enabled:
	 *
	 * If the remote is enabled and should be used.
	 *
	 * Since: 0.9.3
	 */
	pspec = g_param_spec_boolean("enabled",
				     NULL,
				     NULL,
				     FALSE,
				     G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ENABLED, pspec);

	/**
	 * FwupdRemote:approval-required:
	 *
	 * If firmware from the remote should be checked against the system
	 * list of approved firmware.
	 *
	 * Since: 1.2.6
	 */
	pspec = g_param_spec_boolean("approval-required",
				     NULL,
				     NULL,
				     FALSE,
				     G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_APPROVAL_REQUIRED, pspec);

	/**
	 * FwupdRemote:automatic-reports:
	 *
	 * The behavior for auto-uploading reports.
	 *
	 * Since: 1.3.3
	 */
	pspec = g_param_spec_boolean("automatic-reports",
				     NULL,
				     NULL,
				     FALSE,
				     G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_AUTOMATIC_REPORTS, pspec);

	/**
	 * FwupdRemote:automatic-security-reports:
	 *
	 * The behavior for auto-uploading security reports.
	 *
	 * Since: 1.5.0
	 */
	pspec = g_param_spec_boolean("automatic-security-reports",
				     NULL,
				     NULL,
				     FALSE,
				     G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_AUTOMATIC_SECURITY_REPORTS, pspec);
}

static void
fwupd_remote_init(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	priv->keyring_kind = FWUPD_KEYRING_KIND_JCAT;
}

static void
fwupd_remote_finalize(GObject *obj)
{
	FwupdRemote *self = FWUPD_REMOTE(obj);
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_free(priv->id);
	g_free(priv->metadata_uri);
	g_free(priv->metadata_uri_sig);
	g_free(priv->firmware_base_uri);
	g_free(priv->report_uri);
	g_free(priv->security_report_uri);
	g_free(priv->username);
	g_free(priv->password);
	g_free(priv->title);
	g_free(priv->agreement);
	g_free(priv->remotes_dir);
	g_free(priv->checksum);
	g_free(priv->filename_cache);
	g_free(priv->filename_cache_sig);
	g_free(priv->filename_source);
	g_strfreev(priv->order_after);
	g_strfreev(priv->order_before);

	G_OBJECT_CLASS(fwupd_remote_parent_class)->finalize(obj);
}

/**
 * fwupd_remote_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new remote using serialized data.
 *
 * Returns: (transfer full): a new #FwupdRemote, or %NULL if @value was invalid
 *
 * Since: 1.0.0
 **/
FwupdRemote *
fwupd_remote_from_variant(GVariant *value)
{
	FwupdRemote *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	type_string = g_variant_get_type_string(value);
	if (g_strcmp0(type_string, "(a{sv})") == 0) {
		rel = fwupd_remote_new();
		g_variant_get(value, "(a{sv})", &iter);
		fwupd_remote_set_from_variant_iter(rel, iter);
		fwupd_remote_set_from_variant_iter(rel, iter);
	} else if (g_strcmp0(type_string, "a{sv}") == 0) {
		rel = fwupd_remote_new();
		g_variant_get(value, "a{sv}", &iter);
		fwupd_remote_set_from_variant_iter(rel, iter);
	} else {
		g_warning("type %s not known", type_string);
	}
	return rel;
}

/**
 * fwupd_remote_array_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates an array of new devices using serialized data.
 *
 * Returns: (transfer container) (element-type FwupdRemote): remotes, or %NULL if @value was invalid
 *
 * Since: 1.2.10
 **/
GPtrArray *
fwupd_remote_array_from_variant(GVariant *value)
{
	GPtrArray *remotes = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	g_return_val_if_fail(value != NULL, NULL);

	remotes = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	untuple = g_variant_get_child_value(value, 0);
	sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		g_autoptr(GVariant) data = g_variant_get_child_value(untuple, i);
		FwupdRemote *remote = fwupd_remote_from_variant(data);
		g_ptr_array_add(remotes, remote);
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
fwupd_remote_new(void)
{
	FwupdRemote *self;
	self = g_object_new(FWUPD_TYPE_REMOTE, NULL);
	return FWUPD_REMOTE(self);
}
