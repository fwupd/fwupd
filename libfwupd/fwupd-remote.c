/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <curl/curl.h>
#include <jcat.h>

#include "fwupd-codec.h"
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
	FwupdRemoteFlags flags;
	gchar *id;
	gchar *firmware_base_uri;
	gchar *report_uri;
	gchar *metadata_uri;
	gchar *metadata_uri_sig;
	gchar *username;
	gchar *password;
	gchar *title;
	gchar *privacy_uri;
	gchar *agreement;
	gchar *checksum;     /* of metadata */
	gchar *checksum_sig; /* of the signature */
	gchar *filename_cache;
	gchar *filename_cache_sig;
	gchar *filename_source;
	gint priority;
	guint64 mtime;
	guint64 refresh_interval;
	gchar **order_after;
	gchar **order_before;
	gchar *remotes_dir;
} FwupdRemotePrivate;

enum {
	PROP_0,
	PROP_ID,
	PROP_ENABLED,
	PROP_APPROVAL_REQUIRED,
	PROP_AUTOMATIC_REPORTS,
	PROP_AUTOMATIC_SECURITY_REPORTS,
	PROP_FLAGS,
	PROP_LAST
};

static void
fwupd_remote_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FwupdRemote,
		       fwupd_remote,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FwupdRemote)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fwupd_remote_codec_iface_init));

#define GET_PRIVATE(o) (fwupd_remote_get_instance_private(o))

typedef gchar curlptr;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(curlptr, curl_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)

/**
 * fwupd_remote_flag_to_string:
 * @flag: remote attribute flags, e.g. %FWUPD_REMOTE_FLAG_ENABLED
 *
 * Returns the printable string for the flag.
 *
 * Returns: string, or %NULL
 *
 * Since: 1.9.4
 **/
const gchar *
fwupd_remote_flag_to_string(FwupdRemoteFlags flag)
{
	if (flag == FWUPD_REMOTE_FLAG_NONE)
		return "none";
	if (flag == FWUPD_REMOTE_FLAG_ENABLED)
		return "enabled";
	if (flag == FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED)
		return "approval-required";
	if (flag == FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS)
		return "automatic-reports";
	if (flag == FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS)
		return "automatic-security-reports";
	if (flag == FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA)
		return "allow-p2p-metadata";
	if (flag == FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE)
		return "allow-p2p-firmware";
	return NULL;
}

/**
 * fwupd_remote_flag_from_string:
 * @flag: (nullable): a string, e.g. `enabled`
 *
 * Converts a string to an enumerated flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.9.4
 **/
FwupdRemoteFlags
fwupd_remote_flag_from_string(const gchar *flag)
{
	if (g_strcmp0(flag, "enabled") == 0)
		return FWUPD_REMOTE_FLAG_ENABLED;
	if (g_strcmp0(flag, "approval-required") == 0)
		return FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED;
	if (g_strcmp0(flag, "automatic-reports") == 0)
		return FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS;
	if (g_strcmp0(flag, "automatic-security-reports") == 0)
		return FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS;
	if (g_strcmp0(flag, "allow-p2p-metadata") == 0)
		return FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA;
	if (g_strcmp0(flag, "allow-p2p-firmware") == 0)
		return FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE;
	return FWUPD_REMOTE_FLAG_NONE;
}

static void
fwupd_remote_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FwupdRemote *self = FWUPD_REMOTE(codec);
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	fwupd_codec_json_append(builder, "Id", priv->id);
	if (priv->kind != FWUPD_REMOTE_KIND_UNKNOWN) {
		fwupd_codec_json_append(builder, "Kind", fwupd_remote_kind_to_string(priv->kind));
	}
	fwupd_codec_json_append(builder, "ReportUri", priv->report_uri);
	fwupd_codec_json_append(builder, "MetadataUri", priv->metadata_uri);
	fwupd_codec_json_append(builder, "MetadataUriSig", priv->metadata_uri_sig);
	fwupd_codec_json_append(builder, "FirmwareBaseUri", priv->firmware_base_uri);
	fwupd_codec_json_append(builder, "Username", priv->username);
	fwupd_codec_json_append(builder, "Password", priv->password);
	fwupd_codec_json_append(builder, "Title", priv->title);
	fwupd_codec_json_append(builder, "PrivacyUri", priv->privacy_uri);
	fwupd_codec_json_append(builder, "Agreement", priv->agreement);
	fwupd_codec_json_append(builder, "Checksum", priv->checksum);
	fwupd_codec_json_append(builder, "ChecksumSig", priv->checksum_sig);
	fwupd_codec_json_append(builder, "FilenameCache", priv->filename_cache);
	fwupd_codec_json_append(builder, "FilenameCacheSig", priv->filename_cache_sig);
	fwupd_codec_json_append(builder, "FilenameSource", priv->filename_source);
	fwupd_codec_json_append_int(builder, "Flags", priv->flags);
	fwupd_codec_json_append_bool(builder,
				     "Enabled",
				     fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_ENABLED));
	fwupd_codec_json_append_bool(
	    builder,
	    "ApprovalRequired",
	    fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED));
	fwupd_codec_json_append_bool(
	    builder,
	    "AutomaticReports",
	    fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS));
	fwupd_codec_json_append_bool(
	    builder,
	    "AutomaticSecurityReports",
	    fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS));
	fwupd_codec_json_append_int(builder, "Priority", priv->priority);
	fwupd_codec_json_append_int(builder, "Mtime", priv->mtime);
	fwupd_codec_json_append_int(builder, "RefreshInterval", priv->refresh_interval);
	fwupd_codec_json_append(builder, "RemotesDir", priv->remotes_dir);
	fwupd_codec_json_append_strv(builder, "OrderAfter", priv->order_after);
	fwupd_codec_json_append_strv(builder, "OrderBefore", priv->order_before);
}

/**
 * fwupd_remote_get_flags:
 * @self: a #FwupdRemote
 *
 * Gets the self flags.
 *
 * Returns: remote attribute flags, or 0 if unset
 *
 * Since: 1.9.4
 **/
FwupdRemoteFlags
fwupd_remote_get_flags(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), 0);
	return priv->flags;
}

/**
 * fwupd_remote_set_flags:
 * @self: a #FwupdRemote
 * @flags: remote attribute flags, e.g. %FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED
 *
 * Sets the attribute flags.
 *
 * Since: 1.9.4
 **/
void
fwupd_remote_set_flags(FwupdRemote *self, FwupdRemoteFlags flags)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	if (flags == priv->flags)
		return;
	priv->flags = flags;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_remote_add_flag:
 * @self: a #FwupdRemote
 * @flag: the #FwupdRemoteFlags, e.g. %FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED
 *
 * Adds a specific attribute flag to the attribute.
 *
 * Since: 1.9.4
 **/
void
fwupd_remote_add_flag(FwupdRemote *self, FwupdRemoteFlags flag)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	priv->flags |= flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_remote_remove_flag:
 * @self: a #FwupdRemote
 * @flag: the #FwupdRemoteFlags, e.g. %FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED
 *
 * Removes a specific attribute flag from the remote.
 *
 * Since: 1.9.4
 **/
void
fwupd_remote_remove_flag(FwupdRemote *self, FwupdRemoteFlags flag)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	priv->flags &= ~flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_remote_has_flag:
 * @self: a #FwupdRemote
 * @flag: the remote flag, e.g. %FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED
 *
 * Finds if the remote has a specific flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.9.4
 **/
gboolean
fwupd_remote_has_flag(FwupdRemote *self, FwupdRemoteFlags flag)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	return (priv->flags & flag) > 0;
}

static gchar *
fwupd_remote_strdup_nonempty(const gchar *text)
{
	if (text == NULL || text[0] == '\0')
		return NULL;
	return g_strdup(text);
}

/**
 * fwupd_remote_set_username:
 * @self: a #FwupdRemote
 * @username: (nullable): an optional username
 *
 * Sets the remote username.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_username(FwupdRemote *self, const gchar *username)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->username, username) == 0)
		return;

	g_free(priv->username);
	priv->username = g_strdup(username);
}

/**
 * fwupd_remote_set_title:
 * @self: a #FwupdRemote
 * @title: (nullable): title text, e.g. "Backup"
 *
 * Sets the remote title.
 *
 * Since: 1.8.13
 **/
void
fwupd_remote_set_title(FwupdRemote *self, const gchar *title)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->title, title) == 0)
		return;

	g_free(priv->title);
	priv->title = g_strdup(title);
}

/**
 * fwupd_remote_set_privacy_uri:
 * @self: a #FwupdRemote
 * @privacy_uri: (nullable): privacy URL, e.g. "https://lvfs.readthedocs.io/en/latest/privacy.html"
 *
 * Sets the remote privacy policy URL.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_privacy_uri(FwupdRemote *self, const gchar *privacy_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->privacy_uri, privacy_uri) == 0)
		return;

	g_free(priv->privacy_uri);
	priv->privacy_uri = g_strdup(privacy_uri);
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

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->agreement, agreement) == 0)
		return;

	g_free(priv->agreement);
	priv->agreement = g_strdup(agreement);
}

/**
 * fwupd_remote_set_checksum_sig:
 * @self: a #FwupdRemote
 * @checksum_sig: (nullable): checksum string
 *
 * Sets the remote signature checksum, typically only useful in the self tests.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_checksum_sig(FwupdRemote *self, const gchar *checksum_sig)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->checksum_sig, checksum_sig) == 0)
		return;

	g_free(priv->checksum_sig);
	priv->checksum_sig = g_strdup(checksum_sig);
}

static void
fwupd_remote_set_checksum_sig_metadata(FwupdRemote *self, const gchar *checksum)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->checksum, checksum) == 0)
		return;

	g_free(priv->checksum);
	priv->checksum = g_strdup(checksum);
}

/**
 * fwupd_remote_set_password:
 * @self: a #FwupdRemote
 * @password: (nullable): an optional password
 *
 * Sets the remote password.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_password(FwupdRemote *self, const gchar *password)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->password, password) == 0)
		return;

	g_free(priv->password);
	priv->password = g_strdup(password);
}

/**
 * fwupd_remote_set_kind:
 * @self: a #FwupdRemote
 * @kind: a #FwupdRemoteKind, e.g. #FWUPD_REMOTE_KIND_LOCAL
 *
 * Sets the kind of the remote.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_kind(FwupdRemote *self, FwupdRemoteKind kind)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	priv->kind = kind;
}

/**
 * fwupd_remote_set_id:
 * @self: a #FwupdRemote
 * @id: (nullable): remote ID, e.g. "lvfs"
 *
 * Sets the remote ID.
 *
 * NOTE: the ID has to be set before the URL.
 *
 * Since: 1.9.3
 **/
void
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
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	if (priv->filename_source == filename_source)
		return;
	g_free(priv->filename_source);
	priv->filename_source = g_strdup(filename_source);
}

static gchar *
fwupd_remote_build_uri(FwupdRemote *self,
		       const gchar *base_uri,
		       const gchar *url_noauth,
		       GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	const gchar *path_suffix = NULL;
	g_autoptr(curlptr) tmp_uri = NULL;
	g_autoptr(CURLU) uri = curl_url();

	/* sanity check */
	if (url_noauth == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "no URI set");
		return NULL;
	}

	/* the LVFS can't accept basic auth on an endpoint not expecting authentication */
	if (!g_str_has_suffix(url_noauth, "/auth") &&
	    (priv->username != NULL || priv->password != NULL)) {
		path_suffix = "auth";
	}

	/* create URI, substituting if required */
	if (base_uri != NULL) {
		g_autofree gchar *basename = NULL;
		g_autofree gchar *path_new = NULL;
		g_autoptr(curlptr) path = NULL;
		g_autoptr(CURLU) uri_tmp = curl_url();

		if (curl_url_set(uri_tmp, CURLUPART_URL, url_noauth, 0) != CURLUE_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to parse url '%s'",
				    url_noauth);
			return NULL;
		}
		(void)curl_url_get(uri_tmp, CURLUPART_PATH, &path, 0);
		basename = g_path_get_basename(path);
		path_new = g_build_filename(priv->firmware_base_uri, basename, path_suffix, NULL);
		(void)curl_url_set(uri, CURLUPART_URL, path_new, 0);

	} else if (g_strstr_len(url_noauth, -1, "/") == NULL) {
		g_autofree gchar *basename = NULL;
		g_autofree gchar *path_new = NULL;
		g_autoptr(curlptr) path = NULL;

		/* use the base URI of the metadata to build the full path */
		if (curl_url_set(uri, CURLUPART_URL, priv->metadata_uri, 0) != CURLUE_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to parse url '%s'",
				    priv->metadata_uri);
			return NULL;
		}
		(void)curl_url_get(uri, CURLUPART_PATH, &path, 0);
		basename = g_path_get_dirname(path);
		path_new = g_build_filename(basename, url_noauth, NULL);
		(void)curl_url_set(uri, CURLUPART_URL, path_new, 0);

	} else {
		g_autofree gchar *url = g_build_filename(url_noauth, path_suffix, NULL);

		/* a normal URI */
		if (curl_url_set(uri, CURLUPART_URL, url, 0) != CURLUE_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to parse URI '%s'",
				    url);
			return NULL;
		}
	}

	/* set the escaped username and password */
	if (priv->username != NULL) {
		g_autofree gchar *user_escaped = g_uri_escape_string(priv->username, NULL, FALSE);
		(void)curl_url_set(uri, CURLUPART_USER, user_escaped, 0);
	}
	if (priv->password != NULL) {
		g_autofree gchar *pass_escaped = g_uri_escape_string(priv->password, NULL, FALSE);
		(void)curl_url_set(uri, CURLUPART_PASSWORD, pass_escaped, 0);
	}
	(void)curl_url_get(uri, CURLUPART_URL, &tmp_uri, 0);
	return g_strdup(tmp_uri);
}

/**
 * fwupd_remote_set_metadata_uri:
 * @self: a #FwupdRemote
 * @metadata_uri: (nullable): metadata URI
 *
 * Sets the remote metadata URI.
 *
 * NOTE: This has to be set before the username and password.
 *
 * Since: 1.8.13
 **/
void
fwupd_remote_set_metadata_uri(FwupdRemote *self, const gchar *metadata_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->metadata_uri, metadata_uri) == 0)
		return;

	/* save this so we can export the object as a GVariant */
	g_free(priv->metadata_uri);
	priv->metadata_uri = g_strdup(metadata_uri);

	/* generate the signature URI too */
	g_free(priv->metadata_uri_sig);
	priv->metadata_uri_sig = g_strconcat(metadata_uri, ".jcat", NULL);
}

/**
 * fwupd_remote_set_firmware_base_uri:
 * @self: a #FwupdRemote
 * @firmware_base_uri: (nullable): base URI for firmware
 *
 * Sets the firmware base URI.
 *
 * NOTE: This has to be set after MetadataURI.
 *
 * Since: 2.0.2
 **/
void
fwupd_remote_set_firmware_base_uri(FwupdRemote *self, const gchar *firmware_base_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->firmware_base_uri, firmware_base_uri) == 0)
		return;

	g_free(priv->firmware_base_uri);
	priv->firmware_base_uri = g_strdup(firmware_base_uri);
}

/**
 * fwupd_remote_set_report_uri:
 * @self: a #FwupdRemote
 * @report_uri: (nullable): report URI
 *
 * Sets the report URI.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_report_uri(FwupdRemote *self, const gchar *report_uri)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *report_uri_safe = fwupd_remote_strdup_nonempty(report_uri);

	/* not changed */
	if (g_strcmp0(priv->report_uri, report_uri_safe) == 0)
		return;

	g_free(priv->report_uri);
	priv->report_uri = g_steal_pointer(&report_uri_safe);
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

/**
 * fwupd_remote_set_filename_cache:
 * @self: a #FwupdRemote
 * @filename: (nullable): filename string
 *
 * Sets the remote filename cache filename, typically only useful in the self tests.
 *
 * Since: 1.8.2
 **/
void
fwupd_remote_set_filename_cache(FwupdRemote *self, const gchar *filename)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_REMOTE(self));

	/* not changed */
	if (g_strcmp0(priv->filename_cache, filename) == 0)
		return;

	g_free(priv->filename_cache);
	priv->filename_cache = g_strdup(filename);

	/* create for all non-local remote types */
	if (priv->kind != FWUPD_REMOTE_KIND_LOCAL) {
		g_free(priv->filename_cache_sig);
		priv->filename_cache_sig = g_strconcat(filename, ".jcat", NULL);
	}
}

/**
 * fwupd_remote_set_order_before:
 * @self: a #FwupdRemote
 * @ids: (nullable): optional remote IDs
 *
 * Sets any remotes that should be ordered before this one.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_order_before(FwupdRemote *self, const gchar *ids)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_clear_pointer(&priv->order_before, g_strfreev);
	if (ids != NULL)
		priv->order_before = g_strsplit_set(ids, ",:;", -1);
}

/**
 * fwupd_remote_set_order_after:
 * @self: a #FwupdRemote
 * @ids: (nullable): optional remote IDs
 *
 * Sets any remotes that should be ordered after this one.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_order_after(FwupdRemote *self, const gchar *ids)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_clear_pointer(&priv->order_after, g_strfreev);
	if (ids != NULL)
		priv->order_after = g_strsplit_set(ids, ",:;", -1);
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
		if (priv->metadata_uri == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "metadata URI not set");
			return FALSE;
		}
		if (g_str_has_suffix(priv->metadata_uri, ".xml.zst")) {
			filename_cache =
			    g_build_filename(priv->remotes_dir, priv->id, "firmware.xml.zst", NULL);
		} else if (g_str_has_suffix(priv->metadata_uri, ".xml.xz")) {
			filename_cache =
			    g_build_filename(priv->remotes_dir, priv->id, "firmware.xml.xz", NULL);
		} else {
			filename_cache =
			    g_build_filename(priv->remotes_dir, priv->id, "firmware.xml.gz", NULL);
		}
		fwupd_remote_set_filename_cache(self, filename_cache);
	}

	/* some validation for DIRECTORY types */
	if (priv->kind == FWUPD_REMOTE_KIND_DIRECTORY) {
		if (priv->firmware_base_uri != NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "Directory remotes don't support firmware base URI");
			return FALSE;
		}
	}

	/* load the signature checksum */
	if (priv->filename_cache_sig != NULL &&
	    g_file_test(priv->filename_cache_sig, G_FILE_TEST_EXISTS)) {
		gsize sz = 0;
		g_autofree gchar *buf = NULL;
		g_autoptr(GChecksum) checksum_sig = g_checksum_new(G_CHECKSUM_SHA256);
		if (!g_file_get_contents(priv->filename_cache_sig, &buf, &sz, error)) {
			g_prefix_error_literal(error, "failed to get signature checksum: ");
			return FALSE;
		}
		g_checksum_update(checksum_sig, (guchar *)buf, (gssize)sz);
		fwupd_remote_set_checksum_sig(self, g_checksum_get_string(checksum_sig));
	} else {
		fwupd_remote_set_checksum_sig(self, NULL);
	}

	/* success */
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
 * fwupd_remote_get_refresh_interval:
 * @self: a #FwupdRemote
 *
 * Gets the plugin refresh interval in seconds.
 *
 * Returns: value in seconds
 *
 * Since: 1.9.4
 **/
guint64
fwupd_remote_get_refresh_interval(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), G_MAXUINT64);
	return priv->refresh_interval;
}

/**
 * fwupd_remote_set_refresh_interval:
 * @self: a #FwupdRemote
 * @refresh_interval: value in seconds
 *
 * Sets the plugin refresh interval in seconds.
 *
 * Since: 2.0.0
 **/
void
fwupd_remote_set_refresh_interval(FwupdRemote *self, guint64 refresh_interval)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REMOTE(self));
	priv->refresh_interval = refresh_interval;
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
 * fwupd_remote_get_privacy_uri:
 * @self: a #FwupdRemote
 *
 * Gets the remote privacy policy URL, e.g. `https://lvfs.readthedocs.io/en/latest/privacy.html`
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 2.0.0
 **/
const gchar *
fwupd_remote_get_privacy_uri(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->privacy_uri;
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
 * Gets the remote signature checksum.
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
	return priv->checksum_sig;
}

/**
 * fwupd_remote_get_checksum_metadata:
 * @self: a #FwupdRemote
 *
 * Gets the remote metadata checksum.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.9.4
 **/
const gchar *
fwupd_remote_get_checksum_metadata(FwupdRemote *self)
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
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	g_return_val_if_fail(url != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fwupd_remote_build_uri(self, priv->firmware_base_uri, url, error);
}

/**
 * fwupd_remote_build_report_uri:
 * @self: a #FwupdRemote
 * @error: (nullable): optional return location for an error
 *
 * Builds a URI for the URL using the username and password set for the remote.
 *
 * Returns: (transfer full): a URI, or %NULL for error
 *
 * Since: 1.9.1
 **/
gchar *
fwupd_remote_build_report_uri(FwupdRemote *self, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fwupd_remote_build_uri(self, NULL, priv->report_uri, error);
}

/**
 * fwupd_remote_build_metadata_sig_uri:
 * @self: a #FwupdRemote
 * @error: (nullable): optional return location for an error
 *
 * Builds a URI for the metadata using the username and password set for the remote.
 *
 * Returns: (transfer full): a URI, or %NULL for error
 *
 * Since: 1.9.8
 **/
gchar *
fwupd_remote_build_metadata_sig_uri(FwupdRemote *self, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fwupd_remote_build_uri(self, NULL, priv->metadata_uri_sig, error);
}

/**
 * fwupd_remote_build_metadata_uri:
 * @self: a #FwupdRemote
 * @error: (nullable): optional return location for an error
 *
 * Builds a URI for the metadata signature using the username and password set for the remote.
 *
 * Returns: (transfer full): a URI, or %NULL for error
 *
 * Since: 1.9.8
 **/
gchar *
fwupd_remote_build_metadata_uri(FwupdRemote *self, GError **error)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fwupd_remote_build_uri(self, NULL, priv->metadata_uri, error);
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
	g_autoptr(GPtrArray) jcat_blobs = NULL;
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
		g_info("changing metadata URI from %s to %s", priv->metadata_uri, metadata_uri);
		g_free(priv->metadata_uri);
		priv->metadata_uri = g_steal_pointer(&metadata_uri);
	}

	/* look for the metadata hash */
	jcat_blobs = jcat_item_get_blobs_by_kind(jcat_item, JCAT_BLOB_KIND_SHA256);
	if (jcat_blobs->len == 1) {
		JcatBlob *blob = g_ptr_array_index(jcat_blobs, 0);
		g_autofree gchar *hash = jcat_blob_get_data_as_string(blob);
		fwupd_remote_set_checksum_sig_metadata(self, hash);
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
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_remote_load_signature_bytes(FwupdRemote *self, GBytes *bytes, GError **error)
{
	g_autoptr(GInputStream) istr = NULL;
	g_autoptr(JcatFile) jcat_file = jcat_file_new();

	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

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
	if (!jcat_file_import_file(jcat_file, gfile, JCAT_IMPORT_FLAG_NONE, NULL, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
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
 * Since: 2.0.2
 **/
const gchar *
fwupd_remote_get_firmware_base_uri(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), NULL);
	return priv->firmware_base_uri;
}

/**
 * fwupd_remote_needs_refresh:
 * @self: a #FwupdRemote
 *
 * Gets if the metadata remote needs re-downloading.
 *
 * Returns: a #TRUE if the remote contents are considered old
 *
 * Since: 1.9.4
 **/
gboolean
fwupd_remote_needs_refresh(FwupdRemote *self)
{
	FwupdRemotePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REMOTE(self), FALSE);

	if (!fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_ENABLED))
		return FALSE;
	if (priv->kind != FWUPD_REMOTE_KIND_DOWNLOAD)
		return FALSE;
	return fwupd_remote_get_age(self) > priv->refresh_interval;
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
fwupd_remote_from_variant_iter(FwupdCodec *codec, GVariantIter *iter)
{
	FwupdRemote *self = FWUPD_REMOTE(codec);
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
		if (g_strcmp0(key, FWUPD_RESULT_KEY_FLAGS) == 0)
			fwupd_remote_set_flags(self, g_variant_get_uint64(value));
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
	}
	while (g_variant_iter_loop(iter3, "{&sv}", &key, &value)) {
		if (g_strcmp0(key, "Username") == 0) {
			fwupd_remote_set_username(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Password") == 0) {
			fwupd_remote_set_password(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Title") == 0) {
			fwupd_remote_set_title(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "PrivacyUri") == 0) {
			fwupd_remote_set_privacy_uri(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Agreement") == 0) {
			fwupd_remote_set_agreement(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, FWUPD_RESULT_KEY_CHECKSUM) == 0) {
			fwupd_remote_set_checksum_sig(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "Enabled") == 0) {
			if (g_variant_get_boolean(value))
				fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_ENABLED);
		} else if (g_strcmp0(key, "ApprovalRequired") == 0) {
			if (g_variant_get_boolean(value))
				fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED);
		} else if (g_strcmp0(key, "Priority") == 0) {
			priv->priority = g_variant_get_int32(value);
		} else if (g_strcmp0(key, "ModificationTime") == 0) {
			priv->mtime = g_variant_get_uint64(value);
		} else if (g_strcmp0(key, "RefreshInterval") == 0) {
			priv->refresh_interval = g_variant_get_uint64(value);
		} else if (g_strcmp0(key, "FirmwareBaseUri") == 0) {
			fwupd_remote_set_firmware_base_uri(self, g_variant_get_string(value, NULL));
		} else if (g_strcmp0(key, "AutomaticReports") == 0) {
			/* we can probably stop doing proxying flags when we next branch */
			if (g_variant_get_boolean(value))
				fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS);
		} else if (g_strcmp0(key, "AutomaticSecurityReports") == 0) {
			if (g_variant_get_boolean(value))
				fwupd_remote_add_flag(self,
						      FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS);
		}
	}
}

static void
fwupd_remote_add_variant(FwupdCodec *codec, GVariantBuilder *builder, FwupdCodecFlags flags)
{
	FwupdRemote *self = FWUPD_REMOTE(codec);
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	/* create an array with all the metadata in */
	if (priv->id != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_REMOTE_ID,
				      g_variant_new_string(priv->id));
	}
	if (priv->flags != 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FLAGS,
				      g_variant_new_uint64(priv->flags));
	}
	if (priv->username != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "Username",
				      g_variant_new_string(priv->username));
	}
	if (priv->password != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "Password",
				      g_variant_new_string(priv->password));
	}
	if (priv->title != NULL) {
		g_variant_builder_add(builder, "{sv}", "Title", g_variant_new_string(priv->title));
	}
	if (priv->privacy_uri != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "PrivacyUri",
				      g_variant_new_string(priv->privacy_uri));
	}
	if (priv->agreement != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "Agreement",
				      g_variant_new_string(priv->agreement));
	}
	if (priv->checksum_sig != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CHECKSUM,
				      g_variant_new_string(priv->checksum_sig));
	}
	if (priv->metadata_uri != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_URI,
				      g_variant_new_string(priv->metadata_uri));
	}
	if (priv->report_uri != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "ReportUri",
				      g_variant_new_string(priv->report_uri));
	}
	if (priv->firmware_base_uri != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "FirmwareBaseUri",
				      g_variant_new_string(priv->firmware_base_uri));
	}
	if (priv->priority != 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "Priority",
				      g_variant_new_int32(priv->priority));
	}
	if (priv->kind != FWUPD_REMOTE_KIND_UNKNOWN) {
		g_variant_builder_add(builder, "{sv}", "Type", g_variant_new_uint32(priv->kind));
	}
	if (priv->mtime != 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "ModificationTime",
				      g_variant_new_uint64(priv->mtime));
	}
	if (priv->refresh_interval != 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "RefreshInterval",
				      g_variant_new_uint64(priv->refresh_interval));
	}
	if (priv->filename_cache != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "FilenameCache",
				      g_variant_new_string(priv->filename_cache));
	}
	if (priv->filename_source != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "FilenameSource",
				      g_variant_new_string(priv->filename_source));
	}
	if (priv->remotes_dir != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      "RemotesDir",
				      g_variant_new_string(priv->remotes_dir));
	}
	/* we can probably stop doing proxying flags when we next branch */
	g_variant_builder_add(
	    builder,
	    "{sv}",
	    "Enabled",
	    g_variant_new_boolean(fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_ENABLED)));
	g_variant_builder_add(
	    builder,
	    "{sv}",
	    "ApprovalRequired",
	    g_variant_new_boolean(
		fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED)));
	g_variant_builder_add(
	    builder,
	    "{sv}",
	    "AutomaticReports",
	    g_variant_new_boolean(
		fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS)));
	g_variant_builder_add(
	    builder,
	    "{sv}",
	    "AutomaticSecurityReports",
	    g_variant_new_boolean(
		fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS)));
}

static void
fwupd_remote_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdRemote *self = FWUPD_REMOTE(obj);
	FwupdRemotePrivate *priv = GET_PRIVATE(self);

	switch (prop_id) {
	case PROP_ENABLED:
		g_value_set_boolean(value, fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_ENABLED));
		break;
	case PROP_APPROVAL_REQUIRED:
		g_value_set_boolean(
		    value,
		    fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED));
		break;
	case PROP_ID:
		g_value_set_string(value, priv->id);
		break;
	case PROP_AUTOMATIC_REPORTS:
		g_value_set_boolean(
		    value,
		    fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS));
		break;
	case PROP_AUTOMATIC_SECURITY_REPORTS:
		g_value_set_boolean(
		    value,
		    fwupd_remote_has_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS));
		break;
	case PROP_FLAGS:
		g_value_set_uint64(value, priv->flags);
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

	switch (prop_id) {
	case PROP_ENABLED:
		if (g_value_get_boolean(value))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_ENABLED);
		else
			fwupd_remote_remove_flag(self, FWUPD_REMOTE_FLAG_ENABLED);
		break;
	case PROP_APPROVAL_REQUIRED:
		if (g_value_get_boolean(value))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED);
		else
			fwupd_remote_remove_flag(self, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED);
		break;
	case PROP_ID:
		fwupd_remote_set_id(self, g_value_get_string(value));
		break;
	case PROP_AUTOMATIC_REPORTS:
		if (g_value_get_boolean(value))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS);
		else
			fwupd_remote_remove_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS);
		break;
	case PROP_AUTOMATIC_SECURITY_REPORTS:
		if (g_value_get_boolean(value))
			fwupd_remote_add_flag(self, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS);
		else
			fwupd_remote_remove_flag(self,
						 FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS);
		break;
	case PROP_FLAGS:
		fwupd_remote_set_flags(self, g_value_get_uint64(value));
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

	/**
	 * FwupdRemote:flags:
	 *
	 * The remote flags.
	 *
	 * Since: 1.9.4
	 */
	pspec = g_param_spec_uint64("flags",
				    NULL,
				    NULL,
				    0,
				    G_MAXUINT64,
				    0,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLAGS, pspec);
}

static void
fwupd_remote_init(FwupdRemote *self)
{
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
	g_free(priv->username);
	g_free(priv->password);
	g_free(priv->title);
	g_free(priv->privacy_uri);
	g_free(priv->agreement);
	g_free(priv->remotes_dir);
	g_free(priv->checksum);
	g_free(priv->checksum_sig);
	g_free(priv->filename_cache);
	g_free(priv->filename_cache_sig);
	g_free(priv->filename_source);
	g_strfreev(priv->order_after);
	g_strfreev(priv->order_before);

	G_OBJECT_CLASS(fwupd_remote_parent_class)->finalize(obj);
}

static void
fwupd_remote_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fwupd_remote_add_json;
	iface->add_variant = fwupd_remote_add_variant;
	iface->from_variant_iter = fwupd_remote_from_variant_iter;
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
