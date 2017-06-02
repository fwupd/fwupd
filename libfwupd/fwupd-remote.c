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
	gchar			*filename;
	gchar			*filename_asc;
	gboolean		 enabled;
	SoupURI			*uri;
	SoupURI			*uri_asc;
};

enum {
	PROP_0,
	PROP_ID,
	PROP_ENABLED,
	PROP_LAST
};

G_DEFINE_TYPE (FwupdRemote, fwupd_remote, G_TYPE_OBJECT)

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
	g_autofree gchar *basename = NULL;
	g_autofree gchar *basename_asc = NULL;
	g_autofree gchar *url = NULL;
	g_autofree gchar *url_asc = NULL;
	g_autofree gchar *username = NULL;
	g_autofree gchar *password = NULL;
	g_autoptr(GKeyFile) kf = NULL;

	g_return_val_if_fail (FWUPD_IS_REMOTE (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* set ID */
	self->id = g_path_get_basename (filename);
	g_strdelimit (self->id, ".", '\0');

	/* load file */
	kf = g_key_file_new ();
	if (!g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, error))
		return FALSE;

	/* extract data */
	self->enabled = g_key_file_get_boolean (kf, group, "Enabled", NULL);
	url = g_key_file_get_string (kf, group, "Url", error);
	if (url == NULL)
		return FALSE;
	self->uri = soup_uri_new (url);
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
	if (username != NULL && username[0] != '\0')
		soup_uri_set_user (self->uri, username);
	password = g_key_file_get_string (kf, group, "Password", NULL);
	if (password != NULL && password[0] != '\0')
		soup_uri_set_password (self->uri, password);

	/* generate the signature URI too */
	url_asc = g_strdup_printf ("%s.asc", url);
	self->uri_asc = fwupd_remote_build_uri (self, url_asc, error);
	if (self->uri_asc == NULL)
		return FALSE;

	/* generate some plausible local filenames */
	basename = g_path_get_basename (soup_uri_get_path (self->uri));
	self->filename = g_strdup_printf ("%s-%s", self->id, basename);
	basename_asc = g_path_get_basename (soup_uri_get_path (self->uri_asc));
	self->filename_asc = g_strdup_printf ("%s-%s", self->id, basename_asc);

	/* success */
	return TRUE;
}

const gchar *
fwupd_remote_get_filename (FwupdRemote *self)
{
	g_return_val_if_fail (FWUPD_IS_REMOTE (self), NULL);
	return self->filename;
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
		return FALSE;
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
	g_free (self->filename);
	g_free (self->filename_asc);
	if (self->uri != NULL)
		soup_uri_free (self->uri);
	if (self->uri_asc != NULL)
		soup_uri_free (self->uri_asc);

	G_OBJECT_CLASS (fwupd_remote_parent_class)->finalize (obj);
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
