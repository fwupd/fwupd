/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
	GPtrArray			*categories;
	GPtrArray			*issues;
	GHashTable			*metadata;
	gchar				*description;
	gchar				*filename;
	gchar				*protocol;
	gchar				*homepage;
	gchar				*details_url;
	gchar				*source_url;
	gchar				*appstream_id;
	gchar				*detach_caption;
	gchar				*detach_image;
	gchar				*license;
	gchar				*name;
	gchar				*name_variant_suffix;
	gchar				*summary;
	gchar				*uri;
	gchar				*vendor;
	gchar				*version;
	gchar				*remote_id;
	guint64				 size;
	guint32				 install_duration;
	FwupdReleaseFlags		 flags;
	gchar				*update_message;
} FwupdReleasePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FwupdRelease, fwupd_release, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_release_get_instance_private (o))

/* the deprecated fwupd_release_get_trust_flags() function should only
 * return the last two bits of the #FwupdReleaseFlags */
#define FWUPD_RELEASE_TRUST_FLAGS_MASK		0x3

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
 * fwupd_release_get_update_message:
 * @release: A #FwupdRelease
 *
 * Gets the update message.
 *
 * Returns: the update message, or %NULL if unset
 *
 * Since: 1.2.4
 **/
const gchar *
fwupd_release_get_update_message (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->update_message;
}

/**
 * fwupd_release_set_update_message:
 * @release: A #FwupdRelease
 * @update_message: the update message string
 *
 * Sets the update message.
 *
 * Since: 1.2.4
 **/
void
fwupd_release_set_update_message (FwupdRelease *release, const gchar *update_message)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->update_message);
	priv->update_message = g_strdup (update_message);
}

/**
 * fwupd_release_get_protocol:
 * @release: A #FwupdRelease
 *
 * Gets the update protocol.
 *
 * Returns: the update protocol, or %NULL if unset
 *
 * Since: 1.2.2
 **/
const gchar *
fwupd_release_get_protocol (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->protocol;
}

/**
 * fwupd_release_set_protocol:
 * @release: A #FwupdRelease
 * @protocol: the update protocol, e.g. `org.usb.dfu`
 *
 * Sets the update protocol.
 *
 * Since: 1.2.2
 **/
void
fwupd_release_set_protocol (FwupdRelease *release, const gchar *protocol)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->protocol);
	priv->protocol = g_strdup (protocol);
}

/**
 * fwupd_release_get_issues:
 * @release: A #FwupdRelease
 *
 * Gets the list of issues fixed in this release.
 *
 * Returns: (element-type utf8) (transfer none): the issues, which may be empty
 *
 * Since: 1.3.2
 **/
GPtrArray *
fwupd_release_get_issues (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->issues;
}

/**
 * fwupd_release_add_issue:
 * @release: A #FwupdRelease
 * @issue: the update issue, e.g. `CVE-2019-12345`
 *
 * Adds an resolved issue to this release.
 *
 * Since: 1.3.2
 **/
void
fwupd_release_add_issue (FwupdRelease *release, const gchar *issue)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_return_if_fail (issue != NULL);
	for (guint i = 0; i < priv->issues->len; i++) {
		const gchar *issue_tmp = g_ptr_array_index (priv->issues, i);
		if (g_strcmp0 (issue_tmp, issue) == 0)
			return;
	}
	g_ptr_array_add (priv->issues, g_strdup (issue));
}

/**
 * fwupd_release_get_categories:
 * @release: A #FwupdRelease
 *
 * Gets the release categories.
 *
 * Returns: (element-type utf8) (transfer none): the categories, which may be empty
 *
 * Since: 1.2.7
 **/
GPtrArray *
fwupd_release_get_categories (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->categories;
}

/**
 * fwupd_release_add_category:
 * @release: A #FwupdRelease
 * @category: the update category, e.g. `X-EmbeddedController`
 *
 * Adds the update category.
 *
 * Since: 1.2.7
 **/
void
fwupd_release_add_category (FwupdRelease *release, const gchar *category)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_return_if_fail (category != NULL);
	for (guint i = 0; i < priv->categories->len; i++) {
		const gchar *category_tmp = g_ptr_array_index (priv->categories, i);
		if (g_strcmp0 (category_tmp, category) == 0)
			return;
	}
	g_ptr_array_add (priv->categories, g_strdup (category));
}

/**
 * fwupd_release_has_category:
 * @release: A #FwupdRelease
 * @category: the update category, e.g. `X-EmbeddedController`
 *
 * Finds out if the release has the update category.
 *
 * Returns: %TRUE if the release matches
 *
 * Since: 1.2.7
 **/
gboolean
fwupd_release_has_category (FwupdRelease *release, const gchar *category)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), FALSE);
	g_return_val_if_fail (category != NULL, FALSE);
	for (guint i = 0; i < priv->categories->len; i++) {
		const gchar *category_tmp = g_ptr_array_index (priv->categories, i);
		if (g_strcmp0 (category_tmp, category) == 0)
			return TRUE;
	}
	return FALSE;
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
 * fwupd_release_has_checksum:
 * @release: A #FwupdRelease
 * @checksum: the update checksum
 *
 * Finds out if the release has the update checksum.
 *
 * Returns: %TRUE if the release matches
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_release_has_checksum (FwupdRelease *release, const gchar *checksum)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), FALSE);
	g_return_val_if_fail (checksum != NULL, FALSE);
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum_tmp = g_ptr_array_index (priv->checksums, i);
		if (g_strcmp0 (checksum_tmp, checksum) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_release_get_metadata:
 * @release: A #FwupdRelease
 *
 * Gets the release metadata.
 *
 * Returns: (transfer none): the metadata, which may be empty
 *
 * Since: 1.0.4
 **/
GHashTable *
fwupd_release_get_metadata (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->metadata;
}

/**
 * fwupd_release_add_metadata_item:
 * @release: A #FwupdRelease
 * @key: the key
 * @value: the value
 *
 * Sets a release metadata item.
 *
 * Since: 1.0.4
 **/
void
fwupd_release_add_metadata_item (FwupdRelease *release, const gchar *key, const gchar *value)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_hash_table_insert (priv->metadata, g_strdup (key), g_strdup (value));
}

/**
 * fwupd_release_add_metadata:
 * @release: A #FwupdRelease
 * @hash: the key-values
 *
 * Sets multiple release metadata items.
 *
 * Since: 1.0.4
 **/
void
fwupd_release_add_metadata (FwupdRelease *release, GHashTable *hash)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_autoptr(GList) keys = NULL;

	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_return_if_fail (hash != NULL);

	/* deep copy the whole map */
	keys = g_hash_table_get_keys (hash);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (hash, key);
		g_hash_table_insert (priv->metadata, g_strdup (key), g_strdup (value));
	}
}

/**
 * fwupd_release_get_metadata_item:
 * @release: A #FwupdRelease
 * @key: the key
 *
 * Gets a release metadata item.
 *
 * Returns: the value, or %NULL if unset
 *
 * Since: 1.0.4
 **/
const gchar *
fwupd_release_get_metadata_item (FwupdRelease *release, const gchar *key)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (priv->metadata, key);
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
 * fwupd_release_get_details_url:
 * @release: A #FwupdRelease
 *
 * Gets the URL for the online update notes.
 *
 * Returns: the update URL, or %NULL if unset
 *
 * Since: 1.2.4
 **/
const gchar *
fwupd_release_get_details_url (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->details_url;
}

/**
 * fwupd_release_set_details_url:
 * @release: A #FwupdRelease
 * @details_url: the URL
 *
 * Sets the URL for the online update notes.
 *
 * Since: 1.2.4
 **/
void
fwupd_release_set_details_url (FwupdRelease *release, const gchar *details_url)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->details_url);
	priv->details_url = g_strdup (details_url);
}

/**
 * fwupd_release_get_source_url:
 * @release: A #FwupdRelease
 *
 * Gets the URL of the source code used to build this release.
 *
 * Returns: the update source_url, or %NULL if unset
 *
 * Since: 1.2.4
 **/
const gchar *
fwupd_release_get_source_url (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->source_url;
}

/**
 * fwupd_release_set_source_url:
 * @release: A #FwupdRelease
 * @source_url: the URL
 *
 * Sets the URL of the source code used to build this release.
 *
 * Since: 1.2.4
 **/
void
fwupd_release_set_source_url (FwupdRelease *release, const gchar *source_url)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->source_url);
	priv->source_url = g_strdup (source_url);
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
 * fwupd_release_get_detach_caption:
 * @release: A #FwupdRelease
 *
 * Gets the optional text caption used to manually detach the device.
 *
 * Returns: the string caption, or %NULL if unset
 *
 * Since: 1.3.3
 **/
const gchar *
fwupd_release_get_detach_caption (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->detach_caption;
}

/**
 * fwupd_release_set_detach_caption:
 * @release: A #FwupdRelease
 * @detach_caption: string caption
 *
 * Sets the optional text caption used to manually detach the device.
 *
 * Since: 1.3.3
 **/
void
fwupd_release_set_detach_caption (FwupdRelease *release, const gchar *detach_caption)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->detach_caption);
	priv->detach_caption = g_strdup (detach_caption);
}

/**
 * fwupd_release_get_detach_image:
 * @release: A #FwupdRelease
 *
 * Gets the optional image used to manually detach the device.
 *
 * Returns: the URI, or %NULL if unset
 *
 * Since: 1.3.3
 **/
const gchar *
fwupd_release_get_detach_image (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->detach_image;
}

/**
 * fwupd_release_set_detach_image:
 * @release: A #FwupdRelease
 * @detach_image: a fully qualified URI
 *
 * Sets the optional image used to manually detach the device.
 *
 * Since: 1.3.3
 **/
void
fwupd_release_set_detach_image (FwupdRelease *release, const gchar *detach_image)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->detach_image);
	priv->detach_image = g_strdup (detach_image);
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
 * fwupd_release_get_name_variant_suffix:
 * @release: A #FwupdRelease
 *
 * Gets the update variant suffix.
 *
 * Returns: the update variant, or %NULL if unset
 *
 * Since: 1.3.2
 **/
const gchar *
fwupd_release_get_name_variant_suffix (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	return priv->name_variant_suffix;
}

/**
 * fwupd_release_set_name_variant_suffix:
 * @release: A #FwupdRelease
 * @name_variant_suffix: the description
 *
 * Sets the update variant suffix.
 *
 * Since: 1.3.2
 **/
void
fwupd_release_set_name_variant_suffix (FwupdRelease *release, const gchar *name_variant_suffix)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_free (priv->name_variant_suffix);
	priv->name_variant_suffix = g_strdup (name_variant_suffix);
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
	return priv->flags & FWUPD_RELEASE_TRUST_FLAGS_MASK;
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

	/* only overwrite the last two bits of the flags */
	priv->flags &= ~FWUPD_RELEASE_TRUST_FLAGS_MASK;
	priv->flags |= trust_flags;
}

/**
 * fwupd_release_get_flags:
 * @release: A #FwupdRelease
 *
 * Gets the release flags.
 *
 * Returns: the release flags, or 0 if unset
 *
 * Since: 1.2.6
 **/
FwupdReleaseFlags
fwupd_release_get_flags (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), 0);
	return priv->flags;
}

/**
 * fwupd_release_set_flags:
 * @release: A #FwupdRelease
 * @flags: the release flags, e.g. %FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD
 *
 * Sets the release flags.
 *
 * Since: 1.2.6
 **/
void
fwupd_release_set_flags (FwupdRelease *release, FwupdReleaseFlags flags)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	priv->flags = flags;
}

/**
 * fwupd_release_add_flag:
 * @release: A #FwupdRelease
 * @flag: the #FwupdReleaseFlags
 *
 * Adds a specific release flag to the release.
 *
 * Since: 1.2.6
 **/
void
fwupd_release_add_flag (FwupdRelease *release, FwupdReleaseFlags flag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	priv->flags |= flag;
}

/**
 * fwupd_release_remove_flag:
 * @release: A #FwupdRelease
 * @flag: the #FwupdReleaseFlags
 *
 * Removes a specific release flag from the release.
 *
 * Since: 1.2.6
 **/
void
fwupd_release_remove_flag (FwupdRelease *release, FwupdReleaseFlags flag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	priv->flags &= ~flag;
}

/**
 * fwupd_release_has_flag:
 * @release: A #FwupdRelease
 * @flag: the #FwupdReleaseFlags
 *
 * Finds if the release has a specific release flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_release_has_flag (FwupdRelease *release, FwupdReleaseFlags flag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fwupd_release_get_install_duration:
 * @release: A #FwupdRelease
 *
 * Gets the time estimate for firmware installation (in seconds)
 *
 * Returns: the estimated time to flash this release (or 0 if unset)
 *
 * Since: 1.2.1
 **/
guint32
fwupd_release_get_install_duration (FwupdRelease *release)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), 0);
	return priv->install_duration;
}

/**
 * fwupd_release_set_install_duration:
 * @release: A #FwupdRelease
 * @duration: The amount of time
 *
 * Sets the time estimate for firmware installation (in seconds)
 *
 * Since: 1.2.1
 **/
void
fwupd_release_set_install_duration (FwupdRelease *release, guint32 duration)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	priv->install_duration = duration;
}

static GVariant *
_hash_kv_to_variant (GHashTable *hash)
{
	GVariantBuilder builder;
	g_autoptr(GList) keys = g_hash_table_get_keys (hash);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (hash, key);
		g_variant_builder_add (&builder, "{ss}", key, value);
	}
	return g_variant_builder_end (&builder);
}

static GHashTable *
_variant_to_hash_kv (GVariant *dict)
{
	GHashTable *hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	GVariantIter iter;
	const gchar *key;
	const gchar *value;
	g_variant_iter_init (&iter, dict);
	while (g_variant_iter_loop (&iter, "{ss}", &key, &value))
		g_hash_table_insert (hash, g_strdup (key), g_strdup (value));
	return hash;
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
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
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
	if (priv->detach_caption != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_DETACH_CAPTION,
				       g_variant_new_string (priv->detach_caption));
	}
	if (priv->detach_image != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_DETACH_IMAGE,
				       g_variant_new_string (priv->detach_image));
	}
	if (priv->filename != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_FILENAME,
				       g_variant_new_string (priv->filename));
	}
	if (priv->protocol != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_PROTOCOL,
				       g_variant_new_string (priv->protocol));
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
	if (priv->name_variant_suffix != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX,
				       g_variant_new_string (priv->name_variant_suffix));
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
	if (priv->categories->len > 0) {
		g_autofree const gchar **strv = g_new0 (const gchar *, priv->categories->len + 1);
		for (guint i = 0; i < priv->categories->len; i++)
			strv[i] = (const gchar *) g_ptr_array_index (priv->categories, i);
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_CATEGORIES,
				       g_variant_new_strv (strv, -1));
	}
	if (priv->issues->len > 0) {
		g_autofree const gchar **strv = g_new0 (const gchar *, priv->issues->len + 1);
		for (guint i = 0; i < priv->issues->len; i++)
			strv[i] = (const gchar *) g_ptr_array_index (priv->issues, i);
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_ISSUES,
				       g_variant_new_strv (strv, -1));
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
	if (priv->details_url != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_DETAILS_URL,
				       g_variant_new_string (priv->details_url));
	}
	if (priv->source_url != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_SOURCE_URL,
				       g_variant_new_string (priv->source_url));
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
	if (priv->flags != 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_TRUST_FLAGS,
				       g_variant_new_uint64 (priv->flags));
	}
	if (g_hash_table_size (priv->metadata) > 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_METADATA,
				       _hash_kv_to_variant (priv->metadata));
	}
	if (priv->install_duration > 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_INSTALL_DURATION,
				       g_variant_new_uint32 (priv->install_duration));
	}
	return g_variant_new ("a{sv}", &builder);
}

static void
fwupd_release_from_key_value (FwupdRelease *release, const gchar *key, GVariant *value)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_REMOTE_ID) == 0) {
		fwupd_release_set_remote_id (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_APPSTREAM_ID) == 0) {
		fwupd_release_set_appstream_id (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_DETACH_CAPTION) == 0) {
		fwupd_release_set_detach_caption (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_DETACH_IMAGE) == 0) {
		fwupd_release_set_detach_image (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_FILENAME) == 0) {
		fwupd_release_set_filename (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_PROTOCOL) == 0) {
		fwupd_release_set_protocol (release, g_variant_get_string (value, NULL));
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
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX) == 0) {
		fwupd_release_set_name_variant_suffix (release, g_variant_get_string (value, NULL));
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
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_CATEGORIES) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv (value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_release_add_category (release, strv[i]);
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_ISSUES) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv (value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_release_add_issue (release, strv[i]);
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
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_DETAILS_URL) == 0) {
		fwupd_release_set_details_url (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_SOURCE_URL) == 0) {
		fwupd_release_set_source_url (release, g_variant_get_string (value, NULL));
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
		fwupd_release_set_flags (release, g_variant_get_uint64 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_INSTALL_DURATION) == 0) {
		fwupd_release_set_install_duration (release, g_variant_get_uint32 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_MESSAGE) == 0) {
		fwupd_release_set_update_message (release, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_METADATA) == 0) {
		g_hash_table_unref (priv->metadata);
		priv->metadata = _variant_to_hash_kv (value);
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
fwupd_pad_kv_tfl (GString *str, const gchar *key, FwupdReleaseFlags release_flags)
{
	g_autoptr(GString) tmp = g_string_new ("");
	for (guint i = 0; i < 64; i++) {
		if ((release_flags & ((guint64) 1 << i)) == 0)
			continue;
		g_string_append_printf (tmp, "%s|",
					fwupd_release_flag_to_string ((guint64) 1 << i));
	}
	if (tmp->len == 0) {
		g_string_append (tmp, fwupd_release_flag_to_string (0));
	} else {
		g_string_truncate (tmp, tmp->len - 1);
	}
	fwupd_pad_kv_str (str, key, tmp->str);
}

static void
fwupd_pad_kv_int (GString *str, const gchar *key, guint32 value)
{
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;
	tmp = g_strdup_printf("%" G_GUINT32_FORMAT, value);
	fwupd_pad_kv_str (str, key, tmp);
}

static void
fwupd_release_json_add_string (JsonBuilder *builder, const gchar *key, const gchar *str)
{
	if (str == NULL)
		return;
	json_builder_set_member_name (builder, key);
	json_builder_add_string_value (builder, str);
}

static void
fwupd_release_json_add_int (JsonBuilder *builder, const gchar *key, guint64 num)
{
	if (num == 0)
		return;
	json_builder_set_member_name (builder, key);
	json_builder_add_int_value (builder, num);
}

/**
 * fwupd_release_to_json:
 * @release: A #FwupdRelease
 * @builder: A #JsonBuilder
 *
 * Adds a fwupd release to a JSON builder
 *
 * Since: 1.2.6
 **/
void
fwupd_release_to_json (FwupdRelease *release, JsonBuilder *builder)
{
	FwupdReleasePrivate *priv = GET_PRIVATE (release);
	g_autoptr(GList) keys = NULL;

	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_return_if_fail (builder != NULL);

	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_REMOTE_ID, priv->remote_id);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_SUMMARY, priv->summary);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_VERSION, priv->version);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_FILENAME, priv->filename);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_PROTOCOL, priv->protocol);
	if (priv->categories->len > 0) {
		json_builder_set_member_name (builder, FWUPD_RESULT_KEY_CATEGORIES);
		json_builder_begin_array (builder);
		for (guint i = 0; i < priv->categories->len; i++) {
			const gchar *tmp = g_ptr_array_index (priv->categories, i);
			json_builder_add_string_value (builder, tmp);
		}
		json_builder_end_array (builder);
	}
	if (priv->issues->len > 0) {
		json_builder_set_member_name (builder, FWUPD_RESULT_KEY_ISSUES);
		json_builder_begin_array (builder);
		for (guint i = 0; i < priv->issues->len; i++) {
			const gchar *tmp = g_ptr_array_index (priv->issues, i);
			json_builder_add_string_value (builder, tmp);
		}
		json_builder_end_array (builder);
	}
	if (priv->checksums->len > 0) {
		json_builder_set_member_name (builder, FWUPD_RESULT_KEY_CHECKSUM);
		json_builder_begin_array (builder);
		for (guint i = 0; i < priv->checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index (priv->checksums, i);
			json_builder_add_string_value (builder, checksum);
		}
		json_builder_end_array (builder);
	}
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_LICENSE, priv->license);
	fwupd_release_json_add_int (builder, FWUPD_RESULT_KEY_SIZE, priv->size);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_URI, priv->uri);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_HOMEPAGE, priv->homepage);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_DETAILS_URL, priv->details_url);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_SOURCE_URL, priv->source_url);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	if (priv->flags != FWUPD_RELEASE_FLAG_NONE) {
		json_builder_set_member_name (builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array (builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64) 1 << i)) == 0)
				continue;
			tmp = fwupd_release_flag_to_string ((guint64) 1 << i);
			json_builder_add_string_value (builder, tmp);
		}
		json_builder_end_array (builder);
	}
	fwupd_release_json_add_int (builder, FWUPD_RESULT_KEY_INSTALL_DURATION, priv->install_duration);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_DETACH_CAPTION, priv->detach_caption);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_DETACH_IMAGE, priv->detach_image);
	fwupd_release_json_add_string (builder, FWUPD_RESULT_KEY_UPDATE_MESSAGE, priv->update_message);

	/* metadata */
	keys = g_hash_table_get_keys (priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (priv->metadata, key);
		fwupd_release_json_add_string (builder, key, value);
	}
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
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);

	str = g_string_new ("");
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_REMOTE_ID, priv->remote_id);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_SUMMARY, priv->summary);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_VERSION, priv->version);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_FILENAME, priv->filename);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_PROTOCOL, priv->protocol);
	for (guint i = 0; i < priv->categories->len; i++) {
		const gchar *tmp = g_ptr_array_index (priv->categories, i);
		fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_CATEGORIES, tmp);
	}
	for (guint i = 0; i < priv->issues->len; i++) {
		const gchar *tmp = g_ptr_array_index (priv->issues, i);
		fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_ISSUES, tmp);
	}
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index (priv->checksums, i);
		g_autofree gchar *checksum_display = fwupd_checksum_format_for_display (checksum);
		fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_CHECKSUM, checksum_display);
	}
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_LICENSE, priv->license);
	fwupd_pad_kv_siz (str, FWUPD_RESULT_KEY_SIZE, priv->size);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_URI, priv->uri);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_HOMEPAGE, priv->homepage);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_DETAILS_URL, priv->details_url);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_SOURCE_URL, priv->source_url);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	fwupd_pad_kv_tfl (str, FWUPD_RESULT_KEY_FLAGS, priv->flags);
	fwupd_pad_kv_int (str, FWUPD_RESULT_KEY_INSTALL_DURATION, priv->install_duration);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_DETACH_CAPTION, priv->detach_caption);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_DETACH_IMAGE, priv->detach_image);
	if (priv->update_message != NULL)
		fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UPDATE_MESSAGE, priv->update_message);
	/* metadata */
	keys = g_hash_table_get_keys (priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (priv->metadata, key);
		fwupd_pad_kv_str (str, key, value);
	}

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
	priv->categories = g_ptr_array_new_with_free_func (g_free);
	priv->issues = g_ptr_array_new_with_free_func (g_free);
	priv->checksums = g_ptr_array_new_with_free_func (g_free);
	priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
fwupd_release_finalize (GObject *object)
{
	FwupdRelease *release = FWUPD_RELEASE (object);
	FwupdReleasePrivate *priv = GET_PRIVATE (release);

	g_free (priv->description);
	g_free (priv->filename);
	g_free (priv->protocol);
	g_free (priv->appstream_id);
	g_free (priv->detach_caption);
	g_free (priv->detach_image);
	g_free (priv->license);
	g_free (priv->name);
	g_free (priv->name_variant_suffix);
	g_free (priv->summary);
	g_free (priv->uri);
	g_free (priv->homepage);
	g_free (priv->details_url);
	g_free (priv->source_url);
	g_free (priv->vendor);
	g_free (priv->version);
	g_free (priv->remote_id);
	g_free (priv->update_message);
	g_ptr_array_unref (priv->categories);
	g_ptr_array_unref (priv->issues);
	g_ptr_array_unref (priv->checksums);
	g_hash_table_unref (priv->metadata);

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
 * @value: a #GVariant
 *
 * Creates a new release using packed data.
 *
 * Returns: (transfer full): a new #FwupdRelease, or %NULL if @value was invalid
 *
 * Since: 1.0.0
 **/
FwupdRelease *
fwupd_release_from_variant (GVariant *value)
{
	FwupdRelease *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string (value);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		rel = fwupd_release_new ();
		g_variant_get (value, "(a{sv})", &iter);
		fwupd_release_set_from_variant_iter (rel, iter);
	} else if (g_strcmp0 (type_string, "a{sv}") == 0) {
		rel = fwupd_release_new ();
		g_variant_get (value, "a{sv}", &iter);
		fwupd_release_set_from_variant_iter (rel, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return rel;
}

/**
 * fwupd_release_array_from_variant:
 * @value: a #GVariant
 *
 * Creates an array of new releases using packed data.
 *
 * Returns: (transfer container) (element-type FwupdRelease): releases, or %NULL if @value was invalid
 *
 * Since: 1.2.10
 **/
GPtrArray *
fwupd_release_array_from_variant (GVariant *value)
{
	GPtrArray *array = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	untuple = g_variant_get_child_value (value, 0);
	sz = g_variant_n_children (untuple);
	for (guint i = 0; i < sz; i++) {
		FwupdRelease *rel;
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value (untuple, i);
		rel = fwupd_release_from_variant (data);
		if (rel == NULL)
			continue;
		g_ptr_array_add (array, rel);
	}
	return array;
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
