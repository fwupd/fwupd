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
	gchar			*id;
	gchar			*url;
	gchar			*username;
	gchar			*password;
	gchar			*filename;
	gchar			*filename_asc;
	gchar			*filename_cache;
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

/* note, this has to be set before url */
static void
fwupd_remote_set_id (FwupdRemote *self, const gchar *id)
{
	g_free (self->id);
	self->id = g_strdup (id);
	g_strdelimit (self->id, ".", '\0');

	/* set cache filename */
	g_free (self->filename_cache);
	self->filename_cache = g_build_filename (LOCALSTATEDIR,
						 "lib",
						 "fwupd",
						 "remotes.d",
						 self->id,
						 "metadata.xml.gz",
						 NULL);
}

/* note, this has to be set before username and password */
static void
fwupd_remote_set_url (FwupdRemote *self, const gchar *url)
{
	g_autofree gchar *url_asc = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *basename_asc = NULL;

	/* save this so we can export the object as a GVariant */
	self->url = g_strdup (url);

	/* build the URI */
	self->uri = soup_uri_new (url);
	if (self->uri == NULL)
		return;

	/* generate the signature URI too */
	url_asc = g_strdup_printf ("%s.asc", url);
	self->uri_asc = fwupd_remote_build_uri (self, url_asc, NULL);

	/* generate some plausible local filenames */
	basename = g_path_get_basename (soup_uri_get_path (self->uri));
	self->filename = g_strdup_printf ("%s-%s", self->id, basename);
	basename_asc = g_path_get_basename (soup_uri_get_path (self->uri_asc));
	self->filename_asc = g_strdup_printf ("%s-%s", self->id, basename_asc);
}

/**
 * fwupd_remote_load_from_filename:
 * @self: A #FwupdRemote
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Sets up the self ready for use. Most other methods call this
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
	g_autofree gchar *order_after = NULL;
	g_autofree gchar *order_before = NULL;
	g_autofree gchar *password = NULL;
	g_autofree gchar *url = NULL;
	g_autofree gchar *username = NULL;
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

	/* extract data */
	self->enabled = g_key_file_get_boolean (kf, group, "Enabled", NULL);
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

/* private */
const gchar *
fwupd_remote_get_filename_cache (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->filename_cache;
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
	if (self->mtime != 0) {
		g_variant_builder_add (builder, "{sv}", "ModificationTime",
				       g_variant_new_uint64 (self->mtime));
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
	}
	while (g_variant_iter_loop (iter2, "{sv}", &key, &value)) {
		if (g_strcmp0 (key, "Url") == 0)
			fwupd_remote_set_url (self, g_variant_get_string (value, NULL));
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
