/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <string.h>

#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-release-private.h"

/**
 * FwupdRelease:
 *
 * A firmware release with a specific version.
 *
 * Devices can have more than one release, and the releases are typically
 * ordered by their version.
 *
 * See also: [class@FwupdDevice]
 */

static void
fwupd_release_finalize(GObject *object);

typedef struct {
	GPtrArray *checksums;
	GPtrArray *tags;
	GPtrArray *categories;
	GPtrArray *issues;
	GHashTable *metadata;
	gchar *description;
	gchar *filename;
	gchar *protocol;
	gchar *homepage;
	gchar *details_url;
	gchar *source_url;
	gchar *appstream_id;
	gchar *id;
	gchar *detach_caption;
	gchar *detach_image;
	gchar *license;
	gchar *name;
	gchar *name_variant_suffix;
	gchar *summary;
	gchar *branch;
	GPtrArray *locations;
	gchar *vendor;
	gchar *version;
	gchar *remote_id;
	guint64 size;
	guint64 created;
	guint32 install_duration;
	FwupdReleaseFlags flags;
	FwupdReleaseUrgency urgency;
	gchar *update_message;
	gchar *update_image;
} FwupdReleasePrivate;

enum { PROP_0, PROP_REMOTE_ID, PROP_LAST };

G_DEFINE_TYPE_WITH_PRIVATE(FwupdRelease, fwupd_release, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_release_get_instance_private(o))

/* the deprecated fwupd_release_get_trust_flags() function should only
 * return the last two bits of the #FwupdReleaseFlags */
#define FWUPD_RELEASE_TRUST_FLAGS_MASK 0x3

/**
 * fwupd_release_get_remote_id:
 * @self: a #FwupdRelease
 *
 * Gets the remote ID that can be used for downloading.
 *
 * Returns: the ID, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_remote_id(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->remote_id;
}

/**
 * fwupd_release_set_remote_id:
 * @self: a #FwupdRelease
 * @remote_id: the release ID, e.g. `USB:foo`
 *
 * Sets the remote ID that can be used for downloading.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_remote_id(FwupdRelease *self, const gchar *remote_id)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->remote_id, remote_id) == 0)
		return;

	g_free(priv->remote_id);
	priv->remote_id = g_strdup(remote_id);
	g_object_notify(G_OBJECT(self), "remote-id");
}

/**
 * fwupd_release_get_version:
 * @self: a #FwupdRelease
 *
 * Gets the update version.
 *
 * Returns: the update version, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_version(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->version;
}

/**
 * fwupd_release_set_version:
 * @self: a #FwupdRelease
 * @version: the update version, e.g. `1.2.4`
 *
 * Sets the update version.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_version(FwupdRelease *self, const gchar *version)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->version, version) == 0)
		return;

	g_free(priv->version);
	priv->version = g_strdup(version);
}

/**
 * fwupd_release_get_filename:
 * @self: a #FwupdRelease
 *
 * Gets the update filename.
 *
 * Returns: the update filename, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_filename(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->filename;
}

/**
 * fwupd_release_set_filename:
 * @self: a #FwupdRelease
 * @filename: the update filename on disk
 *
 * Sets the update filename.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_filename(FwupdRelease *self, const gchar *filename)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->filename, filename) == 0)
		return;

	g_free(priv->filename);
	priv->filename = g_strdup(filename);
}

/**
 * fwupd_release_get_update_message:
 * @self: a #FwupdRelease
 *
 * Gets the update message.
 *
 * Returns: the update message, or %NULL if unset
 *
 * Since: 1.2.4
 **/
const gchar *
fwupd_release_get_update_message(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->update_message;
}

/**
 * fwupd_release_set_update_message:
 * @self: a #FwupdRelease
 * @update_message: (nullable): the update message string
 *
 * Sets the update message.
 *
 * Since: 1.2.4
 **/
void
fwupd_release_set_update_message(FwupdRelease *self, const gchar *update_message)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->update_message, update_message) == 0)
		return;

	g_free(priv->update_message);
	priv->update_message = g_strdup(update_message);
}

/**
 * fwupd_release_get_update_image:
 * @self: a #FwupdRelease
 *
 * Gets the update image.
 *
 * Returns: the update image URL, or %NULL if unset
 *
 * Since: 1.4.5
 **/
const gchar *
fwupd_release_get_update_image(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->update_image;
}

/**
 * fwupd_release_set_update_image:
 * @self: a #FwupdRelease
 * @update_image: (nullable): the update image URL
 *
 * Sets the update image.
 *
 * Since: 1.4.5
 **/
void
fwupd_release_set_update_image(FwupdRelease *self, const gchar *update_image)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->update_image, update_image) == 0)
		return;

	g_free(priv->update_image);
	priv->update_image = g_strdup(update_image);
}

/**
 * fwupd_release_get_protocol:
 * @self: a #FwupdRelease
 *
 * Gets the update protocol.
 *
 * Returns: the update protocol, or %NULL if unset
 *
 * Since: 1.2.2
 **/
const gchar *
fwupd_release_get_protocol(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->protocol;
}

/**
 * fwupd_release_set_protocol:
 * @self: a #FwupdRelease
 * @protocol: (nullable): the update protocol, e.g. `org.usb.dfu`
 *
 * Sets the update protocol.
 *
 * Since: 1.2.2
 **/
void
fwupd_release_set_protocol(FwupdRelease *self, const gchar *protocol)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->protocol, protocol) == 0)
		return;

	g_free(priv->protocol);
	priv->protocol = g_strdup(protocol);
}

/**
 * fwupd_release_get_issues:
 * @self: a #FwupdRelease
 *
 * Gets the list of issues fixed in this release.
 *
 * Returns: (element-type utf8) (transfer none): the issues, which may be empty
 *
 * Since: 1.3.2
 **/
GPtrArray *
fwupd_release_get_issues(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->issues;
}

/**
 * fwupd_release_add_issue:
 * @self: a #FwupdRelease
 * @issue: (not nullable): the update issue, e.g. `CVE-2019-12345`
 *
 * Adds an resolved issue to this release.
 *
 * Since: 1.3.2
 **/
void
fwupd_release_add_issue(FwupdRelease *self, const gchar *issue)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(issue != NULL);
	for (guint i = 0; i < priv->issues->len; i++) {
		const gchar *issue_tmp = g_ptr_array_index(priv->issues, i);
		if (g_strcmp0(issue_tmp, issue) == 0)
			return;
	}
	g_ptr_array_add(priv->issues, g_strdup(issue));
}

/**
 * fwupd_release_get_categories:
 * @self: a #FwupdRelease
 *
 * Gets the release categories.
 *
 * Returns: (element-type utf8) (transfer none): the categories, which may be empty
 *
 * Since: 1.2.7
 **/
GPtrArray *
fwupd_release_get_categories(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->categories;
}

/**
 * fwupd_release_add_category:
 * @self: a #FwupdRelease
 * @category: (not nullable): the update category, e.g. `X-EmbeddedController`
 *
 * Adds the update category.
 *
 * Since: 1.2.7
 **/
void
fwupd_release_add_category(FwupdRelease *self, const gchar *category)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(category != NULL);
	for (guint i = 0; i < priv->categories->len; i++) {
		const gchar *category_tmp = g_ptr_array_index(priv->categories, i);
		if (g_strcmp0(category_tmp, category) == 0)
			return;
	}
	g_ptr_array_add(priv->categories, g_strdup(category));
}

/**
 * fwupd_release_has_category:
 * @self: a #FwupdRelease
 * @category: (not nullable): the update category, e.g. `X-EmbeddedController`
 *
 * Finds out if the release has the update category.
 *
 * Returns: %TRUE if the release matches
 *
 * Since: 1.2.7
 **/
gboolean
fwupd_release_has_category(FwupdRelease *self, const gchar *category)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), FALSE);
	g_return_val_if_fail(category != NULL, FALSE);
	for (guint i = 0; i < priv->categories->len; i++) {
		const gchar *category_tmp = g_ptr_array_index(priv->categories, i);
		if (g_strcmp0(category_tmp, category) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_release_get_checksums:
 * @self: a #FwupdRelease
 *
 * Gets the release checksums.
 *
 * Returns: (element-type utf8) (transfer none): the checksums, which may be empty
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_release_get_checksums(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->checksums;
}

/**
 * fwupd_release_add_checksum:
 * @self: a #FwupdRelease
 * @checksum: (not nullable): the update checksum
 *
 * Sets the update checksum.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_add_checksum(FwupdRelease *self, const gchar *checksum)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(checksum != NULL);
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum_tmp = g_ptr_array_index(priv->checksums, i);
		if (g_strcmp0(checksum_tmp, checksum) == 0)
			return;
	}
	g_ptr_array_add(priv->checksums, g_strdup(checksum));
}

/**
 * fwupd_release_has_checksum:
 * @self: a #FwupdRelease
 * @checksum: (not nullable): the update checksum
 *
 * Finds out if the release has the update checksum.
 *
 * Returns: %TRUE if the release matches
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_release_has_checksum(FwupdRelease *self, const gchar *checksum)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), FALSE);
	g_return_val_if_fail(checksum != NULL, FALSE);
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum_tmp = g_ptr_array_index(priv->checksums, i);
		if (g_strcmp0(checksum_tmp, checksum) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_release_get_tags:
 * @self: a #FwupdRelease
 *
 * Gets the release tags.
 *
 * Returns: (element-type utf8) (transfer none): the tags, which may be empty
 *
 * Since: 1.7.3
 **/
GPtrArray *
fwupd_release_get_tags(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->tags;
}

/**
 * fwupd_release_add_tag:
 * @self: a #FwupdRelease
 * @tag: (not nullable): the update tag, e.g. `vendor-factory-2021q1`
 *
 * Adds a specific release tag.
 *
 * Since: 1.7.3
 **/
void
fwupd_release_add_tag(FwupdRelease *self, const gchar *tag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(tag != NULL);
	for (guint i = 0; i < priv->tags->len; i++) {
		const gchar *tag_tmp = g_ptr_array_index(priv->tags, i);
		if (g_strcmp0(tag_tmp, tag) == 0)
			return;
	}
	g_ptr_array_add(priv->tags, g_strdup(tag));
}

/**
 * fwupd_release_has_tag:
 * @self: a #FwupdRelease
 * @tag: (not nullable): the update tag, e.g. `vendor-factory-2021q1`
 *
 * Finds out if the release has a specific tag.
 *
 * Returns: %TRUE if the release matches
 *
 * Since: 1.7.3
 **/
gboolean
fwupd_release_has_tag(FwupdRelease *self, const gchar *tag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), FALSE);
	g_return_val_if_fail(tag != NULL, FALSE);
	for (guint i = 0; i < priv->tags->len; i++) {
		const gchar *tag_tmp = g_ptr_array_index(priv->tags, i);
		if (g_strcmp0(tag_tmp, tag) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_release_get_metadata:
 * @self: a #FwupdRelease
 *
 * Gets the release metadata.
 *
 * Returns: (transfer none): the metadata, which may be empty
 *
 * Since: 1.0.4
 **/
GHashTable *
fwupd_release_get_metadata(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->metadata;
}

/**
 * fwupd_release_add_metadata_item:
 * @self: a #FwupdRelease
 * @key: (not nullable): the key
 * @value: (not nullable): the value
 *
 * Sets a release metadata item.
 *
 * Since: 1.0.4
 **/
void
fwupd_release_add_metadata_item(FwupdRelease *self, const gchar *key, const gchar *value)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_hash_table_insert(priv->metadata, g_strdup(key), g_strdup(value));
}

/**
 * fwupd_release_add_metadata:
 * @self: a #FwupdRelease
 * @hash: (not nullable): the key-values
 *
 * Sets multiple release metadata items.
 *
 * Since: 1.0.4
 **/
void
fwupd_release_add_metadata(FwupdRelease *self, GHashTable *hash)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GList) keys = NULL;

	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(hash != NULL);

	/* deep copy the whole map */
	keys = g_hash_table_get_keys(hash);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(hash, key);
		g_hash_table_insert(priv->metadata, g_strdup(key), g_strdup(value));
	}
}

/**
 * fwupd_release_get_metadata_item:
 * @self: a #FwupdRelease
 * @key: (not nullable): the key
 *
 * Gets a release metadata item.
 *
 * Returns: the value, or %NULL if unset
 *
 * Since: 1.0.4
 **/
const gchar *
fwupd_release_get_metadata_item(FwupdRelease *self, const gchar *key)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	return g_hash_table_lookup(priv->metadata, key);
}

/**
 * fwupd_release_get_uri:
 * @self: a #FwupdRelease
 *
 * Gets the default update URI.
 *
 * Returns: the update URI, or %NULL if unset
 *
 * Since: 0.9.3
 * Deprecated: 1.5.6: Use fwupd_release_get_locations() instead.
 **/
const gchar *
fwupd_release_get_uri(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	if (priv->locations->len == 0)
		return NULL;
	return (const gchar *)g_ptr_array_index(priv->locations, 0);
}

/**
 * fwupd_release_set_uri:
 * @self: a #FwupdRelease
 * @uri: the update URI
 *
 * Sets the update URI, i.e. where you can download the firmware from.
 *
 * Since: 0.9.3
 * Deprecated: 1.5.6: Use fwupd_release_add_location() instead.
 **/
void
fwupd_release_set_uri(FwupdRelease *self, const gchar *uri)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_ptr_array_set_size(priv->locations, 0);
	g_ptr_array_add(priv->locations, g_strdup(uri));
}

/**
 * fwupd_release_get_locations:
 * @self: a #FwupdRelease
 *
 * Gets the update URI, i.e. where you can download the firmware from.
 *
 * Typically the first URI will be the main HTTP mirror, but all URIs may not
 * be valid HTTP URIs. For example, "ipns://QmSrPmba" is valid here.
 *
 * Returns: (element-type utf8) (transfer none): the URIs
 *
 * Since: 1.5.6
 **/
GPtrArray *
fwupd_release_get_locations(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->locations;
}

/**
 * fwupd_release_add_location:
 * @self: a #FwupdRelease
 * @location: (not nullable): the update URI
 *
 * Adds an update URI, i.e. where you can download the firmware from.
 *
 * Since: 1.5.6
 **/
void
fwupd_release_add_location(FwupdRelease *self, const gchar *location)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(location != NULL);
	for (guint i = 0; i < priv->locations->len; i++) {
		const gchar *location_tmp = g_ptr_array_index(priv->locations, i);
		if (g_strcmp0(location_tmp, location) == 0)
			return;
	}
	g_ptr_array_add(priv->locations, g_strdup(location));
}

/**
 * fwupd_release_get_homepage:
 * @self: a #FwupdRelease
 *
 * Gets the update homepage.
 *
 * Returns: the update homepage, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_homepage(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->homepage;
}

/**
 * fwupd_release_set_homepage:
 * @self: a #FwupdRelease
 * @homepage: (nullable): the URL
 *
 * Sets the update homepage URL.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_homepage(FwupdRelease *self, const gchar *homepage)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->homepage, homepage) == 0)
		return;

	g_free(priv->homepage);
	priv->homepage = g_strdup(homepage);
}

/**
 * fwupd_release_get_details_url:
 * @self: a #FwupdRelease
 *
 * Gets the URL for the online update notes.
 *
 * Returns: the update URL, or %NULL if unset
 *
 * Since: 1.2.4
 **/
const gchar *
fwupd_release_get_details_url(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->details_url;
}

/**
 * fwupd_release_set_details_url:
 * @self: a #FwupdRelease
 * @details_url: (nullable): the URL
 *
 * Sets the URL for the online update notes.
 *
 * Since: 1.2.4
 **/
void
fwupd_release_set_details_url(FwupdRelease *self, const gchar *details_url)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->details_url, details_url) == 0)
		return;

	g_free(priv->details_url);
	priv->details_url = g_strdup(details_url);
}

/**
 * fwupd_release_get_source_url:
 * @self: a #FwupdRelease
 *
 * Gets the URL of the source code used to build this release.
 *
 * Returns: the update source_url, or %NULL if unset
 *
 * Since: 1.2.4
 **/
const gchar *
fwupd_release_get_source_url(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->source_url;
}

/**
 * fwupd_release_set_source_url:
 * @self: a #FwupdRelease
 * @source_url: (nullable): the URL
 *
 * Sets the URL of the source code used to build this release.
 *
 * Since: 1.2.4
 **/
void
fwupd_release_set_source_url(FwupdRelease *self, const gchar *source_url)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->source_url, source_url) == 0)
		return;

	g_free(priv->source_url);
	priv->source_url = g_strdup(source_url);
}

/**
 * fwupd_release_get_description:
 * @self: a #FwupdRelease
 *
 * Gets the update description in AppStream markup format.
 *
 * Returns: the update description, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_description(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->description;
}

/**
 * fwupd_release_set_description:
 * @self: a #FwupdRelease
 * @description: (nullable): the update description in AppStream markup format
 *
 * Sets the update description.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_description(FwupdRelease *self, const gchar *description)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->description, description) == 0)
		return;

	g_free(priv->description);
	priv->description = g_strdup(description);
}

/**
 * fwupd_release_get_appstream_id:
 * @self: a #FwupdRelease
 *
 * Gets the AppStream ID.
 *
 * Returns: the AppStream ID, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_appstream_id(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->appstream_id;
}

/**
 * fwupd_release_set_appstream_id:
 * @self: a #FwupdRelease
 * @appstream_id: (nullable): the AppStream component ID, e.g. `org.hughski.ColorHug2.firmware`
 *
 * Sets the AppStream ID.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_appstream_id(FwupdRelease *self, const gchar *appstream_id)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->appstream_id, appstream_id) == 0)
		return;

	g_free(priv->appstream_id);
	priv->appstream_id = g_strdup(appstream_id);
}

/**
 * fwupd_release_get_id:
 * @self: a #FwupdRelease
 *
 * Gets the release ID, which allows identifying the specific uploaded component.
 *
 * Returns: the ID, or %NULL if unset
 *
 * Since: 1.7.2
 **/
const gchar *
fwupd_release_get_id(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->id;
}

/**
 * fwupd_release_set_id:
 * @self: a #FwupdRelease
 * @id: (nullable): the AppStream component ID, e.g. `component:1234`
 *
 * Sets the ID, which allows identifying the specific uploaded component.
 *
 * Since: 1.7.2
 **/
void
fwupd_release_set_id(FwupdRelease *self, const gchar *id)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->id, id) == 0)
		return;

	g_free(priv->id);
	priv->id = g_strdup(id);
}

/**
 * fwupd_release_get_detach_caption:
 * @self: a #FwupdRelease
 *
 * Gets the optional text caption used to manually detach the device.
 *
 * Returns: the string caption, or %NULL if unset
 *
 * Since: 1.3.3
 **/
const gchar *
fwupd_release_get_detach_caption(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->detach_caption;
}

/**
 * fwupd_release_set_detach_caption:
 * @self: a #FwupdRelease
 * @detach_caption: (nullable): string caption
 *
 * Sets the optional text caption used to manually detach the device.
 *
 * Since: 1.3.3
 **/
void
fwupd_release_set_detach_caption(FwupdRelease *self, const gchar *detach_caption)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->detach_caption, detach_caption) == 0)
		return;

	g_free(priv->detach_caption);
	priv->detach_caption = g_strdup(detach_caption);
}

/**
 * fwupd_release_get_detach_image:
 * @self: a #FwupdRelease
 *
 * Gets the optional image used to manually detach the device.
 *
 * Returns: the URI, or %NULL if unset
 *
 * Since: 1.3.3
 **/
const gchar *
fwupd_release_get_detach_image(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->detach_image;
}

/**
 * fwupd_release_set_detach_image:
 * @self: a #FwupdRelease
 * @detach_image: (nullable): a fully qualified URI
 *
 * Sets the optional image used to manually detach the device.
 *
 * Since: 1.3.3
 **/
void
fwupd_release_set_detach_image(FwupdRelease *self, const gchar *detach_image)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->detach_image, detach_image) == 0)
		return;

	g_free(priv->detach_image);
	priv->detach_image = g_strdup(detach_image);
}

/**
 * fwupd_release_get_size:
 * @self: a #FwupdRelease
 *
 * Gets the update size.
 *
 * Returns: the update size in bytes, or 0 if unset
 *
 * Since: 0.9.3
 **/
guint64
fwupd_release_get_size(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), 0);
	return priv->size;
}

/**
 * fwupd_release_set_size:
 * @self: a #FwupdRelease
 * @size: the update size in bytes
 *
 * Sets the update size.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_size(FwupdRelease *self, guint64 size)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	priv->size = size;
}

/**
 * fwupd_release_get_created:
 * @self: a #FwupdRelease
 *
 * Gets when the update was created.
 *
 * Returns: UTC timestamp in UNIX format, or 0 if unset
 *
 * Since: 1.4.0
 **/
guint64
fwupd_release_get_created(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), 0);
	return priv->created;
}

/**
 * fwupd_release_set_created:
 * @self: a #FwupdRelease
 * @created: UTC timestamp in UNIX format
 *
 * Sets when the update was created.
 *
 * Since: 1.4.0
 **/
void
fwupd_release_set_created(FwupdRelease *self, guint64 created)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	priv->created = created;
}

/**
 * fwupd_release_get_summary:
 * @self: a #FwupdRelease
 *
 * Gets the update summary.
 *
 * Returns: the update summary, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_summary(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->summary;
}

/**
 * fwupd_release_set_summary:
 * @self: a #FwupdRelease
 * @summary: (nullable): the update one line summary
 *
 * Sets the update summary.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_summary(FwupdRelease *self, const gchar *summary)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->summary, summary) == 0)
		return;

	g_free(priv->summary);
	priv->summary = g_strdup(summary);
}

/**
 * fwupd_release_get_branch:
 * @self: a #FwupdRelease
 *
 * Gets the update branch.
 *
 * Returns: the alternate branch, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_release_get_branch(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->branch;
}

/**
 * fwupd_release_set_branch:
 * @self: a #FwupdRelease
 * @branch: (nullable): the update one line branch
 *
 * Sets the alternate branch.
 *
 * Since: 1.5.0
 **/
void
fwupd_release_set_branch(FwupdRelease *self, const gchar *branch)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->branch, branch) == 0)
		return;

	g_free(priv->branch);
	priv->branch = g_strdup(branch);
}

/**
 * fwupd_release_get_vendor:
 * @self: a #FwupdRelease
 *
 * Gets the update vendor.
 *
 * Returns: the update vendor, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_vendor(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->vendor;
}

/**
 * fwupd_release_set_vendor:
 * @self: a #FwupdRelease
 * @vendor: (nullable): the vendor name, e.g. `Hughski Limited`
 *
 * Sets the update vendor.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_vendor(FwupdRelease *self, const gchar *vendor)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->vendor, vendor) == 0)
		return;

	g_free(priv->vendor);
	priv->vendor = g_strdup(vendor);
}

/**
 * fwupd_release_get_license:
 * @self: a #FwupdRelease
 *
 * Gets the update license.
 *
 * Returns: the update license, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_license(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->license;
}

/**
 * fwupd_release_set_license:
 * @self: a #FwupdRelease
 * @license: (nullable): the update license.
 *
 * Sets the update license.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_license(FwupdRelease *self, const gchar *license)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->license, license) == 0)
		return;

	g_free(priv->license);
	priv->license = g_strdup(license);
}

/**
 * fwupd_release_get_name:
 * @self: a #FwupdRelease
 *
 * Gets the update name.
 *
 * Returns: the update name, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_release_get_name(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->name;
}

/**
 * fwupd_release_set_name:
 * @self: a #FwupdRelease
 * @name: (nullable): the update name.
 *
 * Sets the update name.
 *
 * Since: 0.9.3
 **/
void
fwupd_release_set_name(FwupdRelease *self, const gchar *name)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->name, name) == 0)
		return;

	g_free(priv->name);
	priv->name = g_strdup(name);
}

/**
 * fwupd_release_get_name_variant_suffix:
 * @self: a #FwupdRelease
 *
 * Gets the update variant suffix.
 *
 * Returns: the update variant, or %NULL if unset
 *
 * Since: 1.3.2
 **/
const gchar *
fwupd_release_get_name_variant_suffix(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);
	return priv->name_variant_suffix;
}

/**
 * fwupd_release_set_name_variant_suffix:
 * @self: a #FwupdRelease
 * @name_variant_suffix: (nullable): the description
 *
 * Sets the update variant suffix.
 *
 * Since: 1.3.2
 **/
void
fwupd_release_set_name_variant_suffix(FwupdRelease *self, const gchar *name_variant_suffix)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(priv->name_variant_suffix, name_variant_suffix) == 0)
		return;

	g_free(priv->name_variant_suffix);
	priv->name_variant_suffix = g_strdup(name_variant_suffix);
}

/**
 * fwupd_release_get_trust_flags:
 * @self: a #FwupdRelease
 *
 * Gets the trust level of the release.
 *
 * Returns: the trust bitfield, e.g. #FWUPD_TRUST_FLAG_PAYLOAD
 *
 * Since: 0.9.8
 **/
FwupdTrustFlags
fwupd_release_get_trust_flags(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), 0);
	return priv->flags & FWUPD_RELEASE_TRUST_FLAGS_MASK;
}

/**
 * fwupd_release_set_trust_flags:
 * @self: a #FwupdRelease
 * @trust_flags: the bitfield, e.g. #FWUPD_TRUST_FLAG_PAYLOAD
 *
 * Sets the trust level of the release.
 *
 * Since: 0.9.8
 **/
void
fwupd_release_set_trust_flags(FwupdRelease *self, FwupdTrustFlags trust_flags)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));

	/* only overwrite the last two bits of the flags */
	priv->flags &= ~FWUPD_RELEASE_TRUST_FLAGS_MASK;
	priv->flags |= trust_flags;
}

/**
 * fwupd_release_get_flags:
 * @self: a #FwupdRelease
 *
 * Gets the release flags.
 *
 * Returns: release flags, or 0 if unset
 *
 * Since: 1.2.6
 **/
FwupdReleaseFlags
fwupd_release_get_flags(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), 0);
	return priv->flags;
}

/**
 * fwupd_release_set_flags:
 * @self: a #FwupdRelease
 * @flags: release flags, e.g. %FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD
 *
 * Sets the release flags.
 *
 * Since: 1.2.6
 **/
void
fwupd_release_set_flags(FwupdRelease *self, FwupdReleaseFlags flags)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	priv->flags = flags;
}

/**
 * fwupd_release_add_flag:
 * @self: a #FwupdRelease
 * @flag: the #FwupdReleaseFlags
 *
 * Adds a specific release flag to the release.
 *
 * Since: 1.2.6
 **/
void
fwupd_release_add_flag(FwupdRelease *self, FwupdReleaseFlags flag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	priv->flags |= flag;
}

/**
 * fwupd_release_remove_flag:
 * @self: a #FwupdRelease
 * @flag: the #FwupdReleaseFlags
 *
 * Removes a specific release flag from the release.
 *
 * Since: 1.2.6
 **/
void
fwupd_release_remove_flag(FwupdRelease *self, FwupdReleaseFlags flag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	priv->flags &= ~flag;
}

/**
 * fwupd_release_has_flag:
 * @self: a #FwupdRelease
 * @flag: the #FwupdReleaseFlags
 *
 * Finds if the release has a specific release flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_release_has_flag(FwupdRelease *self, FwupdReleaseFlags flag)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fwupd_release_get_urgency:
 * @self: a #FwupdRelease
 *
 * Gets the release urgency.
 *
 * Returns: the release urgency, or 0 if unset
 *
 * Since: 1.4.0
 **/
FwupdReleaseUrgency
fwupd_release_get_urgency(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), 0);
	return priv->urgency;
}

/**
 * fwupd_release_set_urgency:
 * @self: a #FwupdRelease
 * @urgency: the release urgency, e.g. %FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD
 *
 * Sets the release urgency.
 *
 * Since: 1.4.0
 **/
void
fwupd_release_set_urgency(FwupdRelease *self, FwupdReleaseUrgency urgency)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	priv->urgency = urgency;
}

/**
 * fwupd_release_get_install_duration:
 * @self: a #FwupdRelease
 *
 * Gets the time estimate for firmware installation (in seconds)
 *
 * Returns: the estimated time to flash this release (or 0 if unset)
 *
 * Since: 1.2.1
 **/
guint32
fwupd_release_get_install_duration(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_RELEASE(self), 0);
	return priv->install_duration;
}

/**
 * fwupd_release_set_install_duration:
 * @self: a #FwupdRelease
 * @duration: amount of time in seconds
 *
 * Sets the time estimate for firmware installation (in seconds)
 *
 * Since: 1.2.1
 **/
void
fwupd_release_set_install_duration(FwupdRelease *self, guint32 duration)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_RELEASE(self));
	priv->install_duration = duration;
}

/**
 * fwupd_release_to_variant:
 * @self: a #FwupdRelease
 *
 * Serialize the release data.
 *
 * Returns: the serialized data, or %NULL for error
 *
 * Since: 1.0.0
 **/
GVariant *
fwupd_release_to_variant(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;

	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->remote_id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_REMOTE_ID,
				      g_variant_new_string(priv->remote_id));
	}
	if (priv->appstream_id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_APPSTREAM_ID,
				      g_variant_new_string(priv->appstream_id));
	}
	if (priv->id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_RELEASE_ID,
				      g_variant_new_string(priv->id));
	}
	if (priv->detach_caption != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DETACH_CAPTION,
				      g_variant_new_string(priv->detach_caption));
	}
	if (priv->detach_image != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DETACH_IMAGE,
				      g_variant_new_string(priv->detach_image));
	}
	if (priv->update_message != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_MESSAGE,
				      g_variant_new_string(priv->update_message));
	}
	if (priv->update_image != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_IMAGE,
				      g_variant_new_string(priv->update_image));
	}
	if (priv->filename != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FILENAME,
				      g_variant_new_string(priv->filename));
	}
	if (priv->protocol != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_PROTOCOL,
				      g_variant_new_string(priv->protocol));
	}
	if (priv->license != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_LICENSE,
				      g_variant_new_string(priv->license));
	}
	if (priv->name != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_NAME,
				      g_variant_new_string(priv->name));
	}
	if (priv->name_variant_suffix != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX,
				      g_variant_new_string(priv->name_variant_suffix));
	}
	if (priv->size != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_SIZE,
				      g_variant_new_uint64(priv->size));
	}
	if (priv->created != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CREATED,
				      g_variant_new_uint64(priv->created));
	}
	if (priv->summary != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_SUMMARY,
				      g_variant_new_string(priv->summary));
	}
	if (priv->branch != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BRANCH,
				      g_variant_new_string(priv->branch));
	}
	if (priv->description != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DESCRIPTION,
				      g_variant_new_string(priv->description));
	}
	if (priv->categories->len > 0) {
		g_autofree const gchar **strv = g_new0(const gchar *, priv->categories->len + 1);
		for (guint i = 0; i < priv->categories->len; i++)
			strv[i] = (const gchar *)g_ptr_array_index(priv->categories, i);
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CATEGORIES,
				      g_variant_new_strv(strv, -1));
	}
	if (priv->issues->len > 0) {
		g_autofree const gchar **strv = g_new0(const gchar *, priv->issues->len + 1);
		for (guint i = 0; i < priv->issues->len; i++)
			strv[i] = (const gchar *)g_ptr_array_index(priv->issues, i);
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_ISSUES,
				      g_variant_new_strv(strv, -1));
	}
	if (priv->checksums->len > 0) {
		g_autoptr(GString) str = g_string_new("");
		for (guint i = 0; i < priv->checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(priv->checksums, i);
			g_string_append_printf(str, "%s,", checksum);
		}
		if (str->len > 0)
			g_string_truncate(str, str->len - 1);
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CHECKSUM,
				      g_variant_new_string(str->str));
	}
	if (priv->locations->len > 0) {
		g_variant_builder_add(
		    &builder,
		    "{sv}",
		    FWUPD_RESULT_KEY_LOCATIONS,
		    g_variant_new_strv((const gchar *const *)priv->locations->pdata,
				       priv->locations->len));
		/* for compatibility */
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_URI,
				      g_variant_new_string(g_ptr_array_index(priv->locations, 0)));
	}
	if (priv->tags->len > 0) {
		g_variant_builder_add(
		    &builder,
		    "{sv}",
		    FWUPD_RESULT_KEY_TAGS,
		    g_variant_new_strv((const gchar *const *)priv->tags->pdata, priv->tags->len));
	}
	if (priv->homepage != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_HOMEPAGE,
				      g_variant_new_string(priv->homepage));
	}
	if (priv->details_url != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DETAILS_URL,
				      g_variant_new_string(priv->details_url));
	}
	if (priv->source_url != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_SOURCE_URL,
				      g_variant_new_string(priv->source_url));
	}
	if (priv->version != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION,
				      g_variant_new_string(priv->version));
	}
	if (priv->vendor != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VENDOR,
				      g_variant_new_string(priv->vendor));
	}
	if (priv->flags != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_TRUST_FLAGS,
				      g_variant_new_uint64(priv->flags));
	}
	if (priv->urgency != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_URGENCY,
				      g_variant_new_uint32(priv->urgency));
	}
	if (g_hash_table_size(priv->metadata) > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_METADATA,
				      fwupd_hash_kv_to_variant(priv->metadata));
	}
	if (priv->install_duration > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_INSTALL_DURATION,
				      g_variant_new_uint32(priv->install_duration));
	}
	return g_variant_new("a{sv}", &builder);
}

static void
fwupd_release_from_key_value(FwupdRelease *self, const gchar *key, GVariant *value)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	if (g_strcmp0(key, FWUPD_RESULT_KEY_REMOTE_ID) == 0) {
		fwupd_release_set_remote_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_APPSTREAM_ID) == 0) {
		fwupd_release_set_appstream_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_RELEASE_ID) == 0) {
		fwupd_release_set_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DETACH_CAPTION) == 0) {
		fwupd_release_set_detach_caption(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DETACH_IMAGE) == 0) {
		fwupd_release_set_detach_image(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FILENAME) == 0) {
		fwupd_release_set_filename(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_PROTOCOL) == 0) {
		fwupd_release_set_protocol(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_LICENSE) == 0) {
		fwupd_release_set_license(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_release_set_name(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX) == 0) {
		fwupd_release_set_name_variant_suffix(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_SIZE) == 0) {
		fwupd_release_set_size(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CREATED) == 0) {
		fwupd_release_set_created(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_SUMMARY) == 0) {
		fwupd_release_set_summary(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BRANCH) == 0) {
		fwupd_release_set_branch(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DESCRIPTION) == 0) {
		fwupd_release_set_description(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CATEGORIES) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_release_add_category(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_ISSUES) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_release_add_issue(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CHECKSUM) == 0) {
		const gchar *checksums = g_variant_get_string(value, NULL);
		g_auto(GStrv) split = g_strsplit(checksums, ",", -1);
		for (guint i = 0; split[i] != NULL; i++)
			fwupd_release_add_checksum(self, split[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_LOCATIONS) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_release_add_location(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_TAGS) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_release_add_tag(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_URI) == 0) {
		fwupd_release_add_location(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_HOMEPAGE) == 0) {
		fwupd_release_set_homepage(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DETAILS_URL) == 0) {
		fwupd_release_set_details_url(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_SOURCE_URL) == 0) {
		fwupd_release_set_source_url(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION) == 0) {
		fwupd_release_set_version(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VENDOR) == 0) {
		fwupd_release_set_vendor(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_TRUST_FLAGS) == 0) {
		fwupd_release_set_flags(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_URGENCY) == 0) {
		fwupd_release_set_urgency(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_INSTALL_DURATION) == 0) {
		fwupd_release_set_install_duration(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_UPDATE_MESSAGE) == 0) {
		fwupd_release_set_update_message(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_UPDATE_IMAGE) == 0) {
		fwupd_release_set_update_image(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_METADATA) == 0) {
		g_hash_table_unref(priv->metadata);
		priv->metadata = fwupd_variant_to_hash_kv(value);
		return;
	}
}

static void
fwupd_pad_kv_str(GString *str, const gchar *key, const gchar *value)
{
	/* ignore */
	if (key == NULL || value == NULL)
		return;
	g_string_append_printf(str, "  %s: ", key);
	for (gsize i = strlen(key); i < 20; i++)
		g_string_append(str, " ");
	g_string_append_printf(str, "%s\n", value);
}

static void
fwupd_pad_kv_unx(GString *str, const gchar *key, guint64 value)
{
	g_autoptr(GDateTime) date = NULL;
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;

	date = g_date_time_new_from_unix_utc((gint64)value);
	tmp = g_date_time_format(date, "%F");
	fwupd_pad_kv_str(str, key, tmp);
}

static void
fwupd_pad_kv_siz(GString *str, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;
	tmp = g_format_size(value);
	fwupd_pad_kv_str(str, key, tmp);
}

static void
fwupd_pad_kv_tfl(GString *str, const gchar *key, FwupdReleaseFlags release_flags)
{
	g_autoptr(GString) tmp = g_string_new("");
	for (guint i = 0; i < 64; i++) {
		if ((release_flags & ((guint64)1 << i)) == 0)
			continue;
		g_string_append_printf(tmp, "%s|", fwupd_release_flag_to_string((guint64)1 << i));
	}
	if (tmp->len == 0) {
		g_string_append(tmp, fwupd_release_flag_to_string(0));
	} else {
		g_string_truncate(tmp, tmp->len - 1);
	}
	fwupd_pad_kv_str(str, key, tmp->str);
}

static void
fwupd_pad_kv_int(GString *str, const gchar *key, guint32 value)
{
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;
	tmp = g_strdup_printf("%" G_GUINT32_FORMAT, value);
	fwupd_pad_kv_str(str, key, tmp);
}

/**
 * fwupd_release_to_json:
 * @self: a #FwupdRelease
 * @builder: a JSON builder
 *
 * Adds a fwupd release to a JSON builder
 *
 * Since: 1.2.6
 **/
void
fwupd_release_to_json(FwupdRelease *self, JsonBuilder *builder)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GList) keys = NULL;

	g_return_if_fail(FWUPD_IS_RELEASE(self));
	g_return_if_fail(builder != NULL);

	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_RELEASE_ID, priv->id);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_REMOTE_ID, priv->remote_id);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX,
				     priv->name_variant_suffix);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_SUMMARY, priv->summary);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_BRANCH, priv->branch);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_VERSION, priv->version);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_FILENAME, priv->filename);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_PROTOCOL, priv->protocol);
	if (priv->categories->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_CATEGORIES);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->categories->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->categories, i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->issues->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_ISSUES);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->issues->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->issues, i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->checksums->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_CHECKSUM);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(priv->checksums, i);
			json_builder_add_string_value(builder, checksum);
		}
		json_builder_end_array(builder);
	}
	if (priv->tags->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_TAGS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->tags->len; i++) {
			const gchar *tag = g_ptr_array_index(priv->tags, i);
			json_builder_add_string_value(builder, tag);
		}
		json_builder_end_array(builder);
	}
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_LICENSE, priv->license);
	if (priv->size > 0)
		fwupd_common_json_add_int(builder, FWUPD_RESULT_KEY_SIZE, priv->size);
	if (priv->created > 0)
		fwupd_common_json_add_int(builder, FWUPD_RESULT_KEY_CREATED, priv->created);
	if (priv->locations->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_LOCATIONS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->locations->len; i++) {
			const gchar *location = g_ptr_array_index(priv->locations, i);
			json_builder_add_string_value(builder, location);
		}
		json_builder_end_array(builder);
		/* for compatibility */
		fwupd_common_json_add_string(builder,
					     FWUPD_RESULT_KEY_URI,
					     (const gchar *)g_ptr_array_index(priv->locations, 0));
	}
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_HOMEPAGE, priv->homepage);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DETAILS_URL, priv->details_url);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_SOURCE_URL, priv->source_url);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	if (priv->flags != FWUPD_RELEASE_FLAG_NONE) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fwupd_release_flag_to_string((guint64)1 << i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->install_duration > 0) {
		fwupd_common_json_add_int(builder,
					  FWUPD_RESULT_KEY_INSTALL_DURATION,
					  priv->install_duration);
	}
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_DETACH_CAPTION,
				     priv->detach_caption);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DETACH_IMAGE, priv->detach_image);
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_UPDATE_MESSAGE,
				     priv->update_message);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_UPDATE_IMAGE, priv->update_image);

	/* metadata */
	keys = g_hash_table_get_keys(priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(priv->metadata, key);
		fwupd_common_json_add_string(builder, key, value);
	}
}

/**
 * fwupd_release_to_string:
 * @self: a #FwupdRelease
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 0.9.3
 **/
gchar *
fwupd_release_to_string(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	GString *str;
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail(FWUPD_IS_RELEASE(self), NULL);

	str = g_string_new("");
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_RELEASE_ID, priv->id);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_REMOTE_ID, priv->remote_id);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_SUMMARY, priv->summary);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_BRANCH, priv->branch);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_VERSION, priv->version);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_FILENAME, priv->filename);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_PROTOCOL, priv->protocol);
	for (guint i = 0; i < priv->categories->len; i++) {
		const gchar *tmp = g_ptr_array_index(priv->categories, i);
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_CATEGORIES, tmp);
	}
	for (guint i = 0; i < priv->issues->len; i++) {
		const gchar *tmp = g_ptr_array_index(priv->issues, i);
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_ISSUES, tmp);
	}
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index(priv->checksums, i);
		g_autofree gchar *checksum_display = fwupd_checksum_format_for_display(checksum);
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_CHECKSUM, checksum_display);
	}
	for (guint i = 0; i < priv->tags->len; i++) {
		const gchar *tag = g_ptr_array_index(priv->tags, i);
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_TAGS, tag);
	}
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_LICENSE, priv->license);
	fwupd_pad_kv_siz(str, FWUPD_RESULT_KEY_SIZE, priv->size);
	fwupd_pad_kv_unx(str, FWUPD_RESULT_KEY_CREATED, priv->created);
	for (guint i = 0; i < priv->locations->len; i++) {
		const gchar *location = g_ptr_array_index(priv->locations, i);
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_URI, location);
	}
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_HOMEPAGE, priv->homepage);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DETAILS_URL, priv->details_url);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_SOURCE_URL, priv->source_url);
	if (priv->urgency != FWUPD_RELEASE_URGENCY_UNKNOWN) {
		fwupd_pad_kv_str(str,
				 FWUPD_RESULT_KEY_URGENCY,
				 fwupd_release_urgency_to_string(priv->urgency));
	}
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	fwupd_pad_kv_tfl(str, FWUPD_RESULT_KEY_FLAGS, priv->flags);
	fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_INSTALL_DURATION, priv->install_duration);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DETACH_CAPTION, priv->detach_caption);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DETACH_IMAGE, priv->detach_image);
	if (priv->update_message != NULL)
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_UPDATE_MESSAGE, priv->update_message);
	if (priv->update_image != NULL)
		fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_UPDATE_IMAGE, priv->update_image);
	/* metadata */
	keys = g_hash_table_get_keys(priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(priv->metadata, key);
		fwupd_pad_kv_str(str, key, value);
	}

	return g_string_free(str, FALSE);
}

static void
fwupd_release_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdRelease *self = FWUPD_RELEASE(obj);
	FwupdReleasePrivate *priv = GET_PRIVATE(self);

	switch (prop_id) {
	case PROP_REMOTE_ID:
		g_value_set_string(value, priv->remote_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fwupd_release_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FwupdRelease *self = FWUPD_RELEASE(obj);

	switch (prop_id) {
	case PROP_REMOTE_ID:
		fwupd_release_set_remote_id(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fwupd_release_class_init(FwupdReleaseClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_release_finalize;
	object_class->get_property = fwupd_release_get_property;
	object_class->set_property = fwupd_release_set_property;

	/**
	 * FwupdRelease:remote-id:
	 *
	 * The remote ID.
	 *
	 * Since: 1.8.0
	 */
	pspec = g_param_spec_string("remote-id",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_REMOTE_ID, pspec);
}

static void
fwupd_release_init(FwupdRelease *self)
{
	FwupdReleasePrivate *priv = GET_PRIVATE(self);
	priv->categories = g_ptr_array_new_with_free_func(g_free);
	priv->issues = g_ptr_array_new_with_free_func(g_free);
	priv->checksums = g_ptr_array_new_with_free_func(g_free);
	priv->tags = g_ptr_array_new_with_free_func(g_free);
	priv->locations = g_ptr_array_new_with_free_func(g_free);
	priv->metadata = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
fwupd_release_finalize(GObject *object)
{
	FwupdRelease *self = FWUPD_RELEASE(object);
	FwupdReleasePrivate *priv = GET_PRIVATE(self);

	g_free(priv->description);
	g_free(priv->filename);
	g_free(priv->protocol);
	g_free(priv->appstream_id);
	g_free(priv->id);
	g_free(priv->detach_caption);
	g_free(priv->detach_image);
	g_free(priv->license);
	g_free(priv->name);
	g_free(priv->name_variant_suffix);
	g_free(priv->summary);
	g_free(priv->branch);
	g_ptr_array_unref(priv->locations);
	g_free(priv->homepage);
	g_free(priv->details_url);
	g_free(priv->source_url);
	g_free(priv->vendor);
	g_free(priv->version);
	g_free(priv->remote_id);
	g_free(priv->update_message);
	g_free(priv->update_image);
	g_ptr_array_unref(priv->categories);
	g_ptr_array_unref(priv->issues);
	g_ptr_array_unref(priv->checksums);
	g_ptr_array_unref(priv->tags);
	g_hash_table_unref(priv->metadata);

	G_OBJECT_CLASS(fwupd_release_parent_class)->finalize(object);
}

static void
fwupd_release_set_from_variant_iter(FwupdRelease *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_release_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

/**
 * fwupd_release_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new release using serialized data.
 *
 * Returns: (transfer full): a new #FwupdRelease, or %NULL if @value was invalid
 *
 * Since: 1.0.0
 **/
FwupdRelease *
fwupd_release_from_variant(GVariant *value)
{
	FwupdRelease *self = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string(value);
	if (g_strcmp0(type_string, "(a{sv})") == 0) {
		self = fwupd_release_new();
		g_variant_get(value, "(a{sv})", &iter);
		fwupd_release_set_from_variant_iter(self, iter);
	} else if (g_strcmp0(type_string, "a{sv}") == 0) {
		self = fwupd_release_new();
		g_variant_get(value, "a{sv}", &iter);
		fwupd_release_set_from_variant_iter(self, iter);
	} else {
		g_warning("type %s not known", type_string);
	}
	return self;
}

/**
 * fwupd_release_array_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates an array of new releases using serialized data.
 *
 * Returns: (transfer container) (element-type FwupdRelease): releases, or %NULL if @value was
 *invalid
 *
 * Since: 1.2.10
 **/
GPtrArray *
fwupd_release_array_from_variant(GVariant *value)
{
	GPtrArray *array = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	g_return_val_if_fail(value != NULL, NULL);

	array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	untuple = g_variant_get_child_value(value, 0);
	sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		FwupdRelease *self;
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value(untuple, i);
		self = fwupd_release_from_variant(data);
		if (self == NULL)
			continue;
		g_ptr_array_add(array, self);
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
fwupd_release_new(void)
{
	FwupdRelease *self;
	self = g_object_new(FWUPD_TYPE_RELEASE, NULL);
	return FWUPD_RELEASE(self);
}
