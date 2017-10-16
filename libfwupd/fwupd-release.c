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

#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-release-private.h"

/**
 * SECTION:fwupd-release
 * @short_description: a firmware release
 *
 * An object that represents a firmware release with a specific version.
 * Devices can have more than one release, and the releases are typically
 * ordered by their version.
 *
 * See also: #FwupdDevice
 */

static void fwupd_release_finalize	 (GObject *object);

typedef struct {
	GPtrArray			*checksums;
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
	FwupdTrustFlags			 trust_flags;
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
 * @remote_id: the release ID, e.g. `USB:foo`
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
 * @version: the update version, e.g. `1.2.4`
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
 * fwupd_release_get_checksums:
 * @release: A #FwupdRelease
 *
 * Gets the release checksums.
 *
 * Returns: (element-type utf8) (transfer none): the checksums, which may be empty
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_release_get_checksums (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->checksums;
}

/**
 * fwupd_release_add_checksum:
 * @release: A #FwupdRelease
 * @checksum: the update checksum
 *
 * Sets the update checksum.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_add_checksum (FwupdRelease *release, const gchar *checksum)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_return_if_fail (checksum != NULL);
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum_tmp = g_ptr_array_index (priv->checksums, i);
		if (g_strcmp0 (checksum_tmp, checksum) == 0)
			return;
	}
	g_ptr_array_add (priv->checksums, g_strdup (checksum));
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
 * @appstream_id: the AppStream component ID, e.g. `org.hughski.ColorHug2.firmware`
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
 * @vendor: the vendor name, e.g. `Hughski Limited`
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

/**
 * fwupd_release_get_trust_flags:
 * @release: A #FwupdRelease
 *
 * Gets the trust level of the release.
 *
 * Returns: the trust bitfield, e.g. #FWUPD_TRUST_FLAG_PAYLOAD
 *
 * Since: 0.9.8
 **/
FwupdTrustFlags
fwupd_release_get_trust_flags (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), 0);
	return priv->trust_flags;
}

/**
 * fwupd_release_set_trust_flags:
 * @release: A #FwupdRelease
 * @trust_flags: the bitfield, e.g. #FWUPD_TRUST_FLAG_PAYLOAD
 *
 * Sets the trust level of the release.
 *
 * Since: 0.9.8
 **/
void
fwupd_release_set_trust_flags (FwupdRelease *release, FwupdTrustFlags trust_flags)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	priv->trust_flags = trust_flags;
}

/**
 * fwupd_release_to_variant:
 * @release: A #FwupdRelease
 *
 * Creates a GVariant from the release data.
 *
 * Returns: the GVariant, or %NULL for error
 *
 * Since: 1.0.0
 **/
GVariant *
fwupd_release_to_variant (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	GVariantBuilder builder;

	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	if (priv->remote_id != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_REMOTE_ID,
				       g_variant_new_string (priv->remote_id));
	}
	if (priv->appstream_id != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_APPSTREAM_ID,
				       g_variant_new_string (priv->appstream_id));
	}
	if (priv->filename != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_FILENAME,
				       g_variant_new_string (priv->filename));
	}
	if (priv->license != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_LICENSE,
				       g_variant_new_string (priv->license));
	}
	if (priv->name != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_NAME,
				       g_variant_new_string (priv->name));
	}
	if (priv->size != 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_SIZE,
				       g_variant_new_uint64 (priv->size));
	}
	if (priv->summary != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_SUMMARY,
				       g_variant_new_string (priv->summary));
	}
	if (priv->description != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_DESCRIPTION,
				       g_variant_new_string (priv->description));
	}
	if (priv->checksums->len > 0) {
		g_autoptr(GString) str = g_string_new ("");
		for (guint i = 0; i < priv->checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index (priv->checksums, i);
			g_string_append_printf (str, "%s,", checksum);
		}
		if (str->len > 0)
			g_string_truncate (str, str->len - 1);
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_CHECKSUM,
				       g_variant_new_string (str->str));
	}
	if (priv->uri != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_URI,
				       g_variant_new_string (priv->uri));
	}
	if (priv->homepage != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_HOMEPAGE,
				       g_variant_new_string (priv->homepage));
	}
	if (priv->version != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_VERSION,
				       g_variant_new_string (priv->version));
	}
	if (priv->vendor != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_VENDOR,
				       g_variant_new_string (priv->vendor));
	}
	if (priv->trust_flags != 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_TRUST_FLAGS,
				       g_variant_new_uint64 (priv->trust_flags));
	}
	return g_variant_new ("a{sv}", &builder);
}

static void
fwupd_release_from_key_value (FwupdRelease *release, const gchar *key, GVariant *value)
{
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_REMOTE_ID) == 0) {
		fwupd_release_set_remote_id (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_APPSTREAM_ID) == 0) {
		fwupd_release_set_appstream_id (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_FILENAME) == 0) {
		fwupd_release_set_filename (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_LICENSE) == 0) {
		fwupd_release_set_license (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_release_set_name (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_SIZE) == 0) {
		fwupd_release_set_size (release, g_variant_get_uint64 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_SUMMARY) == 0) {
		fwupd_release_set_summary (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_DESCRIPTION) == 0) {
		fwupd_release_set_description (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_CHECKSUM) == 0) {
		const gchar *checksums = g_variant_get_string (value, NULL);
		g_auto(GStrv) split = g_strsplit (checksums, ",", -1);
		for (guint i = 0; split[i] != NULL; i++)
			fwupd_release_add_checksum (release, split[i]);
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_URI) == 0) {
		fwupd_release_set_uri (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_HOMEPAGE) == 0) {
		fwupd_release_set_homepage (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_VERSION) == 0) {
		fwupd_release_set_version (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_VENDOR) == 0) {
		fwupd_release_set_vendor (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_TRUST_FLAGS) == 0) {
		fwupd_release_set_trust_flags (release, g_variant_get_uint64 (value));
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
fwupd_pad_kv_siz (GString *str, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;
	tmp = g_format_size (value);
	fwupd_pad_kv_str (str, key, tmp);
}

static void
fwupd_pad_kv_tfl (GString *str, const gchar *key, FwupdTrustFlags trust_flags)
{
	g_autoptr(GString) tmp = g_string_new ("");
	for (guint i = 1; i < FWUPD_TRUST_FLAG_LAST; i *= 2) {
		if ((trust_flags & i) == 0)
			continue;
		g_string_append_printf (tmp, "%s|",
					fwupd_trust_flag_to_string (i));
	}
	if (tmp->len == 0) {
		g_string_append (tmp, fwupd_trust_flag_to_string (0));
	} else {
		g_string_truncate (tmp, tmp->len - 1);
	}
	fwupd_pad_kv_str (str, key, tmp->str);
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
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_REMOTE_ID, priv->remote_id);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_SUMMARY, priv->summary);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_VERSION, priv->version);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_FILENAME, priv->filename);
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index (priv->checksums, i);
		g_autofree gchar *checksum_display = fwupd_checksum_format_for_display (checksum);
		fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_CHECKSUM, checksum_display);
	}
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_LICENSE, priv->license);
	fwupd_pad_kv_siz (str, FWUPD_RESULT_KEY_SIZE, priv->size);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_URI, priv->uri);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_HOMEPAGE, priv->homepage);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	fwupd_pad_kv_tfl (str, FWUPD_RESULT_KEY_TRUST_FLAGS, priv->trust_flags);

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
	priv->checksums = g_ptr_array_new_with_free_func (g_free);
}

static void
fwupd_release_finalize (GObject *object)
{
	FwupdRelease *release = FWUPD_RELEASE (object);
	FwupdReleasePrivate *priv = GET_PRIVATE (release);

	g_free (priv->description);
	g_free (priv->filename);
	g_free (priv->appstream_id);
	g_free (priv->license);
	g_free (priv->name);
	g_free (priv->summary);
	g_free (priv->uri);
	g_free (priv->homepage);
	g_free (priv->vendor);
	g_free (priv->version);
	g_free (priv->remote_id);
	g_ptr_array_unref (priv->checksums);

	G_OBJECT_CLASS (fwupd_release_parent_class)->finalize (object);
}

static void
fwupd_release_set_from_variant_iter (FwupdRelease *release, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next (iter, "{&sv}", &key, &value)) {
		fwupd_release_from_key_value (release, key, value);
		g_variant_unref (value);
	}
}

/**
 * fwupd_release_from_variant:
 * @data: a #GVariant
 *
 * Creates a new release using packed data.
 *
 * Returns: (transfer full): a new #FwupdRelease, or %NULL if @data was invalid
 *
 * Since: 1.0.0
 **/
FwupdRelease *
fwupd_release_from_variant (GVariant *data)
{
	FwupdRelease *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string (data);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		rel = fwupd_release_new ();
		g_variant_get (data, "(a{sv})", &iter);
		fwupd_release_set_from_variant_iter (rel, iter);
	} else if (g_strcmp0 (type_string, "a{sv}") == 0) {
		rel = fwupd_release_new ();
		g_variant_get (data, "a{sv}", &iter);
		fwupd_release_set_from_variant_iter (rel, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return rel;
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
