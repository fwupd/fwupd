/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include <gio/gio.h>
#include <string.h>

#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-release-private.h"

static void fwupd_release_finalize	 (GObject *object);

typedef struct {
	gchar				*checksum;
	GChecksumType			 checksum_kind;
	gchar				*description;
	gchar				*filename;
	gchar				*homepage;
	gchar				*appstream_id;
	gchar				*license;
	gchar				*name;
	gchar				*summary;
	gchar				*uri;
	gchar				*vendor;
	gchar				*version;
	gchar				*remote_id;
	guint64				 size;
} FwupdReleasePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FwupdRelease, fwupd_release, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_release_get_instance_private (o))

/**
 * fwupd_release_get_remote_id:
 * @release: A #FwupdRelease
 *
 * Gets the remote ID that can be used for downloading.
 *
 * Returns: the ID, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_remote_id (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->remote_id;
}

/**
 * fwupd_release_set_remote_id:
 * @release: A #FwupdRelease
 * @remote_id: the release ID, e.g. "USB:foo"
 *
 * Sets the remote ID that can be used for downloading.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_remote_id (FwupdRelease *release, const gchar *remote_id)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->remote_id);
	priv->remote_id = g_strdup (remote_id);
}

/**
 * fwupd_release_get_version:
 * @release: A #FwupdRelease
 *
 * Gets the update version.
 *
 * Returns: the update version, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_version (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->version;
}

/**
 * fwupd_release_set_version:
 * @release: A #FwupdRelease
 * @version: the update version, e.g. "1.2.4"
 *
 * Sets the update version.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_version (FwupdRelease *release, const gchar *version)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->version);
	priv->version = g_strdup (version);
}

/**
 * fwupd_release_get_filename:
 * @release: A #FwupdRelease
 *
 * Gets the update filename.
 *
 * Returns: the update filename, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_filename (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->filename;
}

/**
 * fwupd_release_set_filename:
 * @release: A #FwupdRelease
 * @filename: the update filename on disk
 *
 * Sets the update filename.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_filename (FwupdRelease *release, const gchar *filename)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->filename);
	priv->filename = g_strdup (filename);
}

/**
 * fwupd_release_get_checksum:
 * @release: A #FwupdRelease
 *
 * Gets the update checksum.
 *
 * Returns: the update checksum, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_checksum (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->checksum;
}

/**
 * fwupd_release_set_checksum:
 * @release: A #FwupdRelease
 * @checksum: the update checksum
 *
 * Sets the update checksum.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_checksum (FwupdRelease *release, const gchar *checksum)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->checksum);
	priv->checksum = g_strdup (checksum);
}

/**
 * fwupd_release_get_checksum_kind:
 * @release: A #FwupdRelease
 *
 * Gets the update checkum kind.
 *
 * Returns: the #GChecksumType
 *
 * Since: 0.9.3
 **/
GChecksumType
fwupd_release_get_checksum_kind (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), 0);
	return priv->checksum_kind;
}

/**
 * fwupd_release_set_checksum_kind:
 * @release: A #FwupdRelease
 * @checkum_kind: the checksum kind, e.g. %G_CHECKSUM_SHA1
 *
 * Sets the update checkum kind.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_checksum_kind (FwupdRelease *release, GChecksumType checkum_kind)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	priv->checksum_kind = checkum_kind;
}

/**
 * fwupd_release_get_uri:
 * @release: A #FwupdRelease
 *
 * Gets the update uri.
 *
 * Returns: the update uri, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_uri (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->uri;
}

/**
 * fwupd_release_set_uri:
 * @release: A #FwupdRelease
 * @uri: the update URI
 *
 * Sets the update uri, i.e. where you can download the firmware from.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_uri (FwupdRelease *release, const gchar *uri)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->uri);
	priv->uri = g_strdup (uri);
}

/**
 * fwupd_release_get_homepage:
 * @release: A #FwupdRelease
 *
 * Gets the update homepage.
 *
 * Returns: the update homepage, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_homepage (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->homepage;
}

/**
 * fwupd_release_set_homepage:
 * @release: A #FwupdRelease
 * @homepage: the description
 *
 * Sets the update homepage.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_homepage (FwupdRelease *release, const gchar *homepage)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->homepage);
	priv->homepage = g_strdup (homepage);
}

/**
 * fwupd_release_get_description:
 * @release: A #FwupdRelease
 *
 * Gets the update description in AppStream markup format.
 *
 * Returns: the update description, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_description (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->description;
}

/**
 * fwupd_release_set_description:
 * @release: A #FwupdRelease
 * @description: the update description in AppStream markup format
 *
 * Sets the update description.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_description (FwupdRelease *release, const gchar *description)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->description);
	priv->description = g_strdup (description);
}

/**
 * fwupd_release_get_appstream_id:
 * @release: A #FwupdRelease
 *
 * Gets the AppStream ID.
 *
 * Returns: the AppStream ID, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_appstream_id (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->appstream_id;
}

/**
 * fwupd_release_set_appstream_id:
 * @release: A #FwupdRelease
 * @appstream_id: the AppStream component ID, e.g. "org.hughski.ColorHug2.firmware"
 *
 * Sets the AppStream ID.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_appstream_id (FwupdRelease *release, const gchar *appstream_id)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->appstream_id);
	priv->appstream_id = g_strdup (appstream_id);
}

/**
 * fwupd_release_get_size:
 * @release: A #FwupdRelease
 *
 * Gets the update size.
 *
 * Returns: the update size in bytes, or 0 if unset
 *
 * Since: 0.9.3
 **/
guint64
fwupd_release_get_size (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), 0);
	return priv->size;
}

/**
 * fwupd_release_set_size:
 * @release: A #FwupdRelease
 * @size: the update size in bytes
 *
 * Sets the update size.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_size (FwupdRelease *release, guint64 size)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	priv->size = size;
}

/**
 * fwupd_release_get_summary:
 * @release: A #FwupdRelease
 *
 * Gets the update summary.
 *
 * Returns: the update summary, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_summary (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->summary;
}

/**
 * fwupd_release_set_summary:
 * @release: A #FwupdRelease
 * @summary: the update one line summary
 *
 * Sets the update summary.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_summary (FwupdRelease *release, const gchar *summary)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->summary);
	priv->summary = g_strdup (summary);
}

/**
 * fwupd_release_get_vendor:
 * @release: A #FwupdRelease
 *
 * Gets the update vendor.
 *
 * Returns: the update vendor, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_vendor (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->vendor;
}

/**
 * fwupd_release_set_vendor:
 * @release: A #FwupdRelease
 * @vendor: the vendor name, e.g. "Hughski Limited"
 *
 * Sets the update vendor.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_vendor (FwupdRelease *release, const gchar *vendor)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->vendor);
	priv->vendor = g_strdup (vendor);
}

/**
 * fwupd_release_get_license:
 * @release: A #FwupdRelease
 *
 * Gets the update license.
 *
 * Returns: the update license, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_license (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->license;
}

/**
 * fwupd_release_set_license:
 * @release: A #FwupdRelease
 * @license: the description
 *
 * Sets the update license.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_license (FwupdRelease *release, const gchar *license)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->license);
	priv->license = g_strdup (license);
}

/**
 * fwupd_release_get_name:
 * @release: A #FwupdRelease
 *
 * Gets the update name.
 *
 * Returns: the update name, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_name (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->name;
}

/**
 * fwupd_release_set_name:
 * @release: A #FwupdRelease
 * @name: the description
 *
 * Sets the update name.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_name (FwupdRelease *release, const gchar *name)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->name);
	priv->name = g_strdup (name);
}

void
fwupd_release_to_variant_builder (FwupdRelease *release, GVariantBuilder *builder)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->remote_id != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_REMOTE_ID,
				       g_variant_new_string (priv->remote_id));
	}
	if (priv->appstream_id != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_ID,
				       g_variant_new_string (priv->appstream_id));
	}
	if (priv->filename != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_FILENAME,
				       g_variant_new_string (priv->filename));
	}
	if (priv->license != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_LICENSE,
				       g_variant_new_string (priv->license));
	}
	if (priv->name != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_NAME,
				       g_variant_new_string (priv->name));
	}
	if (priv->size != 0) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_SIZE,
				       g_variant_new_uint64 (priv->size));
	}
	if (priv->summary != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_SUMMARY,
				       g_variant_new_string (priv->summary));
	}
	if (priv->description != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_DESCRIPTION,
				       g_variant_new_string (priv->description));
	}
	if (priv->checksum != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_CHECKSUM,
				       g_variant_new_string (priv->checksum));
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_CHECKSUM_KIND,
				       g_variant_new_uint32 (priv->checksum_kind));
	}
	if (priv->uri != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_URI,
				       g_variant_new_string (priv->uri));
	}
	if (priv->homepage != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_HOMEPAGE,
				       g_variant_new_string (priv->homepage));
	}
	if (priv->version != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_VERSION,
				       g_variant_new_string (priv->version));
	}
	if (priv->vendor != NULL) {
		g_variant_builder_add (builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_VENDOR,
				       g_variant_new_string (priv->vendor));
	}
}

/**
 * fwupd_release_to_data:
 * @release: A #FwupdRelease
 * @type_string: The Gvariant type string, e.g. "a{sv}" or "(a{sv})"
 *
 * Creates a GVariant from the release data.
 *
 * Returns: the GVariant, or %NULL for error
 *
 * Since: 0.9.3
 **/
GVariant *
fwupd_release_to_data (FwupdRelease *release, const gchar *type_string)
{
	GVariantBuilder builder;

	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	g_return_val_if_fail (type_string != NULL, NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	fwupd_release_to_variant_builder (release, &builder);

	/* supported types */
	if (g_strcmp0 (type_string, "a{sv}") == 0)
		return g_variant_new ("a{sv}", &builder);
	if (g_strcmp0 (type_string, "(a{sv})") == 0)
		return g_variant_new ("(a{sv})", &builder);
	return NULL;
}

void
fwupd_release_from_key_value (FwupdRelease *release, const gchar *key, GVariant *value)
{
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_REMOTE_ID) == 0) {
		fwupd_release_set_remote_id (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_ID) == 0) {
		fwupd_release_set_appstream_id (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_FILENAME) == 0) {
		fwupd_release_set_filename (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_LICENSE) == 0) {
		fwupd_release_set_license (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_NAME) == 0) {
		fwupd_release_set_name (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_SIZE) == 0) {
		fwupd_release_set_size (release, g_variant_get_uint64 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_SUMMARY) == 0) {
		fwupd_release_set_summary (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_DESCRIPTION) == 0) {
		fwupd_release_set_description (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_CHECKSUM) == 0) {
		fwupd_release_set_checksum (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_CHECKSUM_KIND) == 0) {
		fwupd_release_set_checksum_kind (release, g_variant_get_uint32 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_URI) == 0) {
		fwupd_release_set_uri (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_HOMEPAGE) == 0) {
		fwupd_release_set_homepage (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_VERSION) == 0) {
		fwupd_release_set_version (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_VENDOR) == 0) {
		fwupd_release_set_vendor (release, g_variant_get_string (value, NULL));
		return;
	}
}

static void
fwupd_pad_kv_str (GString *str, const gchar *key, const gchar *value)
{
	/* ignore */
	if (key == NULL || value == NULL)
		return;
	g_string_append_printf (str, "  %s: ", key);
	for (gsize i = strlen (key); i < 20; i++)
		g_string_append (str, " ");
	g_string_append_printf (str, "%s\n", value);
}

static void
fwupd_pad_kv_csk (GString *str, const gchar *key, GChecksumType checksum_type)
{
	const gchar *tmp = "unknown";
	if (checksum_type == G_CHECKSUM_SHA1)
		tmp = "sha1";
	else if (checksum_type == G_CHECKSUM_SHA256)
		tmp = "sha256";
	else if (checksum_type == G_CHECKSUM_SHA512)
		tmp = "sha512";
	fwupd_pad_kv_str (str, key, tmp);
}

static void
fwupd_pad_kv_siz (GString *str, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;
	tmp = g_format_size (value);
	fwupd_pad_kv_str (str, key, tmp);
}

/**
 * fwupd_release_to_string:
 * @release: A #FwupdRelease
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 0.9.3
 **/
gchar *
fwupd_release_to_string (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	GString *str;

	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);

	str = g_string_new ("");
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_ID, priv->appstream_id);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_REMOTE_ID, priv->remote_id);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_SUMMARY, priv->summary);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_DESCRIPTION, priv->description);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_VERSION, priv->version);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_FILENAME, priv->filename);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_CHECKSUM, priv->checksum);
	if (priv->checksum != NULL)
		fwupd_pad_kv_csk (str, FWUPD_RESULT_KEY_UPDATE_CHECKSUM_KIND, priv->checksum_kind);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_LICENSE, priv->license);
	fwupd_pad_kv_siz (str, FWUPD_RESULT_KEY_UPDATE_SIZE, priv->size);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_URI, priv->uri);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_HOMEPAGE, priv->homepage);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_VENDOR, priv->vendor);

	return g_string_free (str, FALSE);
}

static void
fwupd_release_class_init (FwupdReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fwupd_release_finalize;
}

static void
fwupd_release_init (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	priv->checksum_kind = G_CHECKSUM_SHA1;
}

static void
fwupd_release_finalize (GObject *object)
{
	FwupdRelease *release = FWUPD_RELEASE (object);
	FwupdReleasePrivate *priv = GET_PRIVATE (release);

	g_free (priv->description);
	g_free (priv->filename);
	g_free (priv->checksum);
	g_free (priv->appstream_id);
	g_free (priv->license);
	g_free (priv->name);
	g_free (priv->summary);
	g_free (priv->uri);
	g_free (priv->homepage);
	g_free (priv->vendor);
	g_free (priv->version);
	g_free (priv->remote_id);

	G_OBJECT_CLASS (fwupd_release_parent_class)->finalize (object);
}

static void
fwupd_release_from_variant_iter (FwupdRelease *release, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next (iter, "{&sv}", &key, &value)) {
		fwupd_release_from_key_value (release, key, value);
		g_variant_unref (value);
	}
}

/**
 * fwupd_release_new_from_data:
 * @data: a #GVariant
 *
 * Creates a new release using packed data.
 *
 * Returns: a new #FwupdRelease, or %NULL if @data was invalid
 *
 * Since: 0.9.3
 **/
FwupdRelease *
fwupd_release_new_from_data (GVariant *data)
{
	FwupdRelease *res = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string (data);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		res = fwupd_release_new ();
		g_variant_get (data, "(a{sv})", &iter);
		fwupd_release_from_variant_iter (res, iter);
	} else if (g_strcmp0 (type_string, "a{sv}") == 0) {
		res = fwupd_release_new ();
		g_variant_get (data, "a{sv}", &iter);
		fwupd_release_from_variant_iter (res, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return res;
}

/**
 * fwupd_release_new:
 *
 * Creates a new release.
 *
 * Returns: a new #FwupdRelease
 *
 * Since: 0.9.3
 **/
FwupdRelease *
fwupd_release_new (void)
{
	FwupdRelease *release;
	release = g_object_new (FWUPD_TYPE_RELEASE, NULL);
	return FWUPD_RELEASE (release);
}
