/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-remote-private.h"

static void fwupd_remote_finalize	 (GObject *obj);

struct _FwupdRemote
{
	GObject			 parent_instance;
	FwupdRemoteKind		 kind;
	FwupdKeyringKind	 keyring_kind;
	gchar			*id;
	gchar			*url;
	gchar			*username;
	gchar			*password;
	gchar			*filename;
	gchar			*filename_asc;
	gchar			*filename_cache;
	gchar			*filename_cache_sig;
	gboolean		 enabled;
	SoupURI			*uri;
	SoupURI			*uri_asc;
	gint			 priority;
	guint64			 mtime;
	gchar			**order_after;
	gchar			**order_before;
};

enum {
	PROP_0,
	PROP_ID,
	PROP_ENABLED,
	PROP_LAST
};

G_DEFINE_TYPE (FwupdRemote, fwupd_remote, G_TYPE_OBJECT)

static void
fwupd_remote_set_username (FwupdRemote *self, const gchar *username)
{
	if (username != NULL && username[0] == '\0')
		username = NULL;
	self->username = g_strdup (username);
	if (self->uri != NULL)
		soup_uri_set_user (self->uri, username);
	if (self->uri_asc != NULL)
		soup_uri_set_user (self->uri_asc, username);
}

static void
fwupd_remote_set_password (FwupdRemote *self, const gchar *password)
{
	if (password != NULL && password[0] == '\0')
		password = NULL;
	self->password = g_strdup (password);
	if (self->uri != NULL)
		soup_uri_set_password (self->uri, password);
	if (self->uri_asc != NULL)
		soup_uri_set_password (self->uri_asc, password);
}

static void
fwupd_remote_set_kind (FwupdRemote *self, FwupdRemoteKind kind)
{
	self->kind = kind;
}

static void
fwupd_remote_set_keyring_kind (FwupdRemote *self, FwupdKeyringKind keyring_kind)
{
	self->keyring_kind = keyring_kind;
}

/* note, this has to be set before url */
static void
fwupd_remote_set_id (FwupdRemote *self, const gchar *id)
{
	g_free (self->id);
	self->id = g_strdup (id);
	g_strdelimit (self->id, ".", '\0');
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

/* note, this has to be set before username and password */
static void
fwupd_remote_set_url (FwupdRemote *self, const gchar *url)
{
	const gchar *suffix;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *basename_asc = NULL;

	/* save this so we can export the object as a GVariant */
	self->url = g_strdup (url);

	/* build the URI */
	self->uri = soup_uri_new (url);
	if (self->uri == NULL)
		return;

	/* generate some plausible local filenames */
	basename = g_path_get_basename (soup_uri_get_path (self->uri));
	self->filename = g_strdup_printf ("%s-%s", self->id, basename);

	/* generate the signature URI too */
	suffix = fwupd_remote_get_suffix_for_keyring_kind (self->keyring_kind);
	if (suffix != NULL) {
		g_autofree gchar *url_asc = g_strconcat (url, suffix, NULL);
		self->uri_asc = fwupd_remote_build_uri (self, url_asc, NULL);
		basename_asc = g_path_get_basename (soup_uri_get_path (self->uri_asc));
		self->filename_asc = g_strdup_printf ("%s-%s", self->id, basename_asc);
	}
}

/**
 * fwupd_remote_kind_from_string:
 * @kind: a string, e.g. "download"
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
	return FWUPD_REMOTE_KIND_UNKNOWN;
}

/**
 * fwupd_remote_kind_to_string:
 * @kind: a #FwupdRemoteKind, e.g. %FWUPD_REMOTE_KIND_DOWNLOAD
 *
 * Converts an enumerated type to a printable string.
 *
 * Returns: a string, e.g. "download"
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
	return NULL;
}

static void
fwupd_remote_set_filename_cache (FwupdRemote *self, const gchar *filename)
{
	const gchar *suffix;

	g_return_if_fail (FWUPD_IS_REMOTE (self));

	g_free (self->filename_cache);
	self->filename_cache = g_strdup (filename);

	/* create for all remote types */
	suffix = fwupd_remote_get_suffix_for_keyring_kind (self->keyring_kind);
	if (suffix != NULL) {
		g_free (self->filename_cache_sig);
		self->filename_cache_sig = g_strconcat (filename, suffix, NULL);
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
	const gchar *group = "fwupd Remote";
	g_autofree gchar *id = NULL;
	g_autofree gchar *kind = NULL;
	g_autofree gchar *keyring_kind = NULL;
	g_autofree gchar *order_after = NULL;
	g_autofree gchar *order_before = NULL;
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

	/* get kind, failing back to download */
	kind = g_key_file_get_string (kf, group, "Type", NULL);
	if (kind == NULL) {
		self->kind = FWUPD_REMOTE_KIND_DOWNLOAD;
	} else {
		self->kind = fwupd_remote_kind_from_string (kind);
		if (self->kind == FWUPD_REMOTE_KIND_UNKNOWN) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse type '%s'",
				     kind);
			return FALSE;
		}
	}

	/* get verification type, falling back to GPG */
	keyring_kind = g_key_file_get_string (kf, group, "Keyring", NULL);
	if (keyring_kind == NULL) {
		self->keyring_kind = FWUPD_KEYRING_KIND_GPG;
	} else {
		self->keyring_kind = fwupd_keyring_kind_from_string (keyring_kind);
		if (self->keyring_kind == FWUPD_KEYRING_KIND_UNKNOWN) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse type '%s'",
				     keyring_kind);
			return FALSE;
		}
	}

	/* extract data */
	self->enabled = g_key_file_get_boolean (kf, group, "Enabled", NULL);

	/* DOWNLOAD-type remotes */
	if (self->kind == FWUPD_REMOTE_KIND_DOWNLOAD) {
		g_autofree gchar *filename_cache = NULL;
		g_autofree gchar *url = NULL;
		g_autofree gchar *username = NULL;
		g_autofree gchar *password = NULL;

		/* remotes have to include a valid Url */
		url = g_key_file_get_string (kf, group, "Url", error);
		if (url == NULL)
			return FALSE;
		fwupd_remote_set_url (self, url);

		/* check the URI was valid */
		if (self->uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to parse URI '%s' in %s",
				     url, filename);
			return FALSE;
		}

		/* username and password are optional */
		username = g_key_file_get_string (kf, group, "Username", NULL);
		if (username != NULL)
			fwupd_remote_set_username (self, username);
		password = g_key_file_get_string (kf, group, "Password", NULL);
		if (password != NULL)
			fwupd_remote_set_password (self, password);

		/* set cache to /var/lib... */
		filename_cache = g_build_filename (LOCALSTATEDIR,
						   "lib",
						   "fwupd",
						   "remotes.d",
						   self->id,
						   "metadata.xml.gz",
						   NULL);
		fwupd_remote_set_filename_cache (self, filename_cache);
	}

	/* all LOCAL remotes have to include a valid File */
	if (self->kind == FWUPD_REMOTE_KIND_LOCAL) {
		g_autofree gchar *filename_cache = NULL;
		filename_cache = g_key_file_get_string (kf, group, "File", error);
		if (filename_cache == NULL)
			return FALSE;
		fwupd_remote_set_filename_cache (self, filename_cache);
	}

	/* dep logic */
	order_before = g_key_file_get_string (kf, group, "OrderBefore", NULL);
	if (order_before != NULL)
		self->order_before = g_strsplit_set (order_before, ",:;", -1);
	order_after = g_key_file_get_string (kf, group, "OrderAfter", NULL);
	if (order_after != NULL)
		self->order_after = g_strsplit_set (order_after, ",:;", -1);

	/* success */
	return TRUE;
}

/* private */
gchar **
fwupd_remote_get_order_after (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->order_after;
}

/* private */
gchar **
fwupd_remote_get_order_before (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->order_before;
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
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->filename_cache;
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
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->filename_cache_sig;
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
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	return self->priority;
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
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	return self->kind;
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
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	return self->keyring_kind;
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
	guint64 now;
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), 0);
	now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
	if (self->mtime > now)
		return G_MAXUINT64;
	return now - self->mtime;
}

/* private */
void
fwupd_remote_set_priority (FwupdRemote *self, gint priority)
{
	g_return_if_fail (FWUPD_IS_REMOTE (self));
	self->priority = priority;
}

/* private */
void
fwupd_remote_set_mtime (FwupdRemote *self, guint64 mtime)
{
	g_return_if_fail (FWUPD_IS_REMOTE (self));
	self->mtime = mtime;
}

const gchar *
fwupd_remote_get_filename (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->filename;
}

const gchar *
fwupd_remote_get_username (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->username;
}

const gchar *
fwupd_remote_get_password (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->password;
}

const gchar *
fwupd_remote_get_filename_asc (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->filename_asc;
}

/**
 * fwupd_remote_build_uri:
 * @self: A #FwupdRemote
 * @url: the URL to use
 * @error: the #GError, or %NULL
 *
 * Builds a URI for the URL using the username and password set for the remote.
 *
 * Returns: (transfer full): a #SoupURI, or %NULL for error
 *
 * Since: 0.9.3
 **/
SoupURI *
fwupd_remote_build_uri (FwupdRemote *self, const gchar *url, GError **error)
{
	SoupURI *uri;

	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	g_return_val_if_fail (url != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create URI */
	uri = soup_uri_new (url);
	if (uri == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse URI '%s'", url);
		return NULL;
	}

	/* set the username and password from the metadata URI */
	if (self->uri != NULL) {
		soup_uri_set_user (uri, soup_uri_get_user (self->uri));
		soup_uri_set_password (uri, soup_uri_get_password (self->uri));
	}
	return uri;
}

/**
 * fwupd_remote_get_uri:
 * @self: A #FwupdRemote
 *
 * Gets the URI for the remote metadata.
 *
 * Returns: a #SoupURI, or %NULL for invalid.
 *
 * Since: 0.9.3
 **/
SoupURI *
fwupd_remote_get_uri (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->uri;
}

/**
 * fwupd_remote_get_uri_asc:
 * @self: A #FwupdRemote
 *
 * Gets the URI for the remote signature.
 *
 * Returns: a #SoupURI, or %NULL for invalid.
 *
 * Since: 0.9.3
 **/
SoupURI *
fwupd_remote_get_uri_asc (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->uri_asc;
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
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), FALSE);
	return self->enabled;
}

/**
 * fwupd_remote_get_id:
 * @self: A #FwupdRemote
 *
 * Gets the remote ID, e.g. "lvfs-testing".
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_remote_get_id (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->id;
}

static void
fwupd_remote_to_variant_builder (FwupdRemote *self, GVariantBuilder *builder)
{
	if (self->id != NULL) {
		g_variant_builder_add (builder, "{sv}", "Id",
				       g_variant_new_string (self->id));
	}
	if (self->username != NULL) {
		g_variant_builder_add (builder, "{sv}", "Username",
				       g_variant_new_string (self->username));
	}
	if (self->password != NULL) {
		g_variant_builder_add (builder, "{sv}", "Password",
				       g_variant_new_string (self->password));
	}
	if (self->url != NULL) {
		g_variant_builder_add (builder, "{sv}", "Url",
				       g_variant_new_string (self->url));
	}
	if (self->priority != 0) {
		g_variant_builder_add (builder, "{sv}", "Priority",
				       g_variant_new_int32 (self->priority));
	}
	if (self->kind != FWUPD_REMOTE_KIND_UNKNOWN) {
		g_variant_builder_add (builder, "{sv}", "Type",
				       g_variant_new_uint32 (self->kind));
	}
	if (self->keyring_kind != FWUPD_KEYRING_KIND_UNKNOWN) {
		g_variant_builder_add (builder, "{sv}", "Keyring",
				       g_variant_new_uint32 (self->keyring_kind));
	}
	if (self->mtime != 0) {
		g_variant_builder_add (builder, "{sv}", "ModificationTime",
				       g_variant_new_uint64 (self->mtime));
	}
	if (self->filename_cache != NULL) {
		g_variant_builder_add (builder, "{sv}", "FilenameCache",
				       g_variant_new_string (self->filename_cache));
	}
	g_variant_builder_add (builder, "{sv}", "Enabled",
			       g_variant_new_boolean (self->enabled));
}

static void
fwupd_remote_set_from_variant_iter (FwupdRemote *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	g_autoptr(GVariantIter) iter2 = g_variant_iter_copy (iter);
	g_autoptr(GVariantIter) iter3 = g_variant_iter_copy (iter);

	/* three passes, as we have to construct Id -> Url -> * */
	while (g_variant_iter_loop (iter, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, "Id") == 0)
			fwupd_remote_set_id (self, g_variant_get_string (value, NULL));
		if (g_strcmp0 (key, "Type") == 0)
			fwupd_remote_set_kind (self, g_variant_get_uint32 (value));
		if (g_strcmp0 (key, "Keyring") == 0)
			fwupd_remote_set_keyring_kind (self, g_variant_get_uint32 (value));
	}
	while (g_variant_iter_loop (iter2, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, "Url") == 0)
			fwupd_remote_set_url (self, g_variant_get_string (value, NULL));
		if (g_strcmp0 (key, "FilenameCache") == 0)
			fwupd_remote_set_filename_cache (self, g_variant_get_string (value, NULL));
	}
	while (g_variant_iter_loop (iter3, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, "Username") == 0) {
			fwupd_remote_set_username (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, "Password") == 0) {
			fwupd_remote_set_password (self, g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, "Enabled") == 0) {
			self->enabled = g_variant_get_boolean (value);
		} else if (g_strcmp0 (key, "Priority") == 0) {
			self->priority = g_variant_get_int32 (value);
		} else if (g_strcmp0 (key, "ModificationTime") == 0) {
			self->mtime = g_variant_get_uint64 (value);
		}
	}
}

/**
 * fwupd_remote_to_data:
 * @remote: A #FwupdRemote
 * @type_string: The Gvariant type string, e.g. "a{sv}" or "(a{sv})"
 *
 * Creates a GVariant from the remote data.
 *
 * Returns: the GVariant, or %NULL for error
 *
 * Since: 0.9.5
 **/
GVariant *
fwupd_remote_to_data (FwupdRemote *self, const gchar *type_string)
{
	GVariantBuilder builder;

	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	g_return_val_if_fail (type_string != NULL, NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	fwupd_remote_to_variant_builder (self, &builder);

	/* supported types */
	if (g_strcmp0 (type_string, "a{sv}") == 0)
		return g_variant_new ("a{sv}", &builder);
	if (g_strcmp0 (type_string, "(a{sv})") == 0)
		return g_variant_new ("(a{sv})", &builder);
	return NULL;
}

static void
fwupd_remote_get_property (GObject *obj, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdRemote *self = FWUPD_REMOTE (obj);

	switch (prop_id) {
	case PROP_ENABLED:
		g_value_set_boolean (value, self->enabled);
		break;
	case PROP_ID:
		g_value_set_string (value, self->id);
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

	switch (prop_id) {
	case PROP_ENABLED:
		self->enabled = g_value_get_boolean (value);
		break;
	case PROP_ID:
		self->id = g_value_get_string (value);
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
				     NULL, G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * FwupdRemote:enabled:
	 *
	 * If the remote is enabled and should be used.
	 *
	 * Since: 0.9.3
	 */
	pspec = g_param_spec_boolean ("enabled", NULL, NULL,
				      FALSE, G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ENABLED, pspec);
}

static void
fwupd_remote_init (FwupdRemote *self)
{
}

static void
fwupd_remote_finalize (GObject *obj)
{
	FwupdRemote *self = FWUPD_REMOTE (obj);

	g_free (self->id);
	g_free (self->url);
	g_free (self->username);
	g_free (self->password);
	g_free (self->filename);
	g_free (self->filename_asc);
	g_free (self->filename_cache);
	g_free (self->filename_cache_sig);
	g_strfreev (self->order_after);
	g_strfreev (self->order_before);
	if (self->uri != NULL)
		soup_uri_free (self->uri);
	if (self->uri_asc != NULL)
		soup_uri_free (self->uri_asc);

	G_OBJECT_CLASS (fwupd_remote_parent_class)->finalize (obj);
}

/**
 * fwupd_remote_new_from_data:
 * @data: a #GVariant
 *
 * Creates a new remote using packed data.
 *
 * Returns: a new #FwupdRemote, or %NULL if @data was invalid
 *
 * Since: 0.9.5
 **/
FwupdRemote *
fwupd_remote_new_from_data (GVariant *data)
{
	FwupdRemote *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	type_string = g_variant_get_type_string (data);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		rel = fwupd_remote_new ();
		g_variant_get (data, "(a{sv})", &iter);
		fwupd_remote_set_from_variant_iter (rel, iter);
		fwupd_remote_set_from_variant_iter (rel, iter);
	} else if (g_strcmp0 (type_string, "a{sv}") == 0) {
		rel = fwupd_remote_new ();
		g_variant_get (data, "a{sv}", &iter);
		fwupd_remote_set_from_variant_iter (rel, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return rel;
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
