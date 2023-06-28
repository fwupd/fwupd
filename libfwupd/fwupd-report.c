/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-report-private.h"

/**
 * FwupdReport:
 *
 * A firmware report from a vendor.
 *
 * This is the LVFS formatted report that the fwupd user consumes, NOT the thing that gets uploaded.
 *
 * See also: [class@FwupdRelease]
 */

typedef struct {
	guint64 created;
	gchar *version_old;
	gchar *vendor;
	guint32 vendor_id;
	gchar *device_name;
	gchar *distro_id;
	gchar *distro_version;
	GHashTable *metadata;
	gchar *distro_variant;
	gchar *remote_id;
	FwupdReportFlags flags;
} FwupdReportPrivate;

enum { PROP_0, PROP_FLAGS, PROP_LAST };

G_DEFINE_TYPE_WITH_PRIVATE(FwupdReport, fwupd_report, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_report_get_instance_private(o))

/**
 * fwupd_report_get_created:
 * @self: a #FwupdReport
 *
 * Gets when the report was created.
 *
 * Returns: UTC timestamp in UNIX format, or 0 if unset
 *
 * Since: 1.8.8
 **/
guint64
fwupd_report_get_created(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), 0);
	return priv->created;
}

/**
 * fwupd_report_set_created:
 * @self: a #FwupdReport
 * @created: UTC timestamp in UNIX format
 *
 * Sets when the report was created.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_created(FwupdReport *self, guint64 created)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));
	priv->created = created;
}

/**
 * fwupd_report_get_version_old:
 * @self: a #FwupdReport
 *
 * Gets the old version, i.e. what the upser was upgrading *from*.
 *
 * Returns: the version, or %NULL if unset
 *
 * Since: 1.8.8
 **/
const gchar *
fwupd_report_get_version_old(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->version_old;
}

/**
 * fwupd_report_set_version_old:
 * @self: a #FwupdReport
 * @version_old: (nullable): the version, e.g. `1.2.3`
 *
 * Sets the old version, i.e. what the upser was upgrading *from*.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_version_old(FwupdReport *self, const gchar *version_old)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));

	/* not changed */
	if (g_strcmp0(priv->version_old, version_old) == 0)
		return;

	g_free(priv->version_old);
	priv->version_old = g_strdup(version_old);
}

/**
 * fwupd_report_get_vendor:
 * @self: a #FwupdReport
 *
 * Gets the vendor that uploaded the test result.
 *
 * Returns: the test vendor, or %NULL if unset
 *
 * Since: 1.8.8
 **/
const gchar *
fwupd_report_get_vendor(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->vendor;
}

/**
 * fwupd_report_set_vendor:
 * @self: a #FwupdReport
 * @vendor: (nullable): the vendor name
 *
 * Sets the vendor that uploaded the test result.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_vendor(FwupdReport *self, const gchar *vendor)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));

	/* not changed */
	if (g_strcmp0(priv->vendor, vendor) == 0)
		return;

	g_free(priv->vendor);
	priv->vendor = g_strdup(vendor);
}

/**
 * fwupd_report_get_vendor_id:
 * @self: a #FwupdReport
 *
 * Gets the vendor identifier. The mapping is only known on the remote server, and this can be
 * useful to filter on different QA teams that work for the same OEM.
 *
 * Returns: the vendor ID, or 0 if unset
 *
 * Since: 1.8.8
 **/
guint32
fwupd_report_get_vendor_id(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), 0);
	return priv->vendor_id;
}

/**
 * fwupd_report_set_vendor_id:
 * @self: a #FwupdReport
 * @vendor_id: the vendor ID, or 0
 *
 * Sets the vendor identifier. The mapping is only known on the remote server, and this can be
 * useful to filter on different QA teams that work for the same OEM.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_vendor_id(FwupdReport *self, guint32 vendor_id)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));
	priv->vendor_id = vendor_id;
}

/**
 * fwupd_report_get_device_name:
 * @self: a #FwupdReport
 *
 * Gets the name of the device the update was performed on.
 *
 * Returns: the name, or %NULL if unset
 *
 * Since: 1.8.8
 **/
const gchar *
fwupd_report_get_device_name(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->device_name;
}

/**
 * fwupd_report_set_device_name:
 * @self: a #FwupdReport
 * @device_name: (nullable): the name, e.g. `LENOVO ThinkPad P1 Gen 3`
 *
 * Sets the name of the device the update was performed on.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_device_name(FwupdReport *self, const gchar *device_name)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));

	/* not changed */
	if (g_strcmp0(priv->device_name, device_name) == 0)
		return;

	g_free(priv->device_name);
	priv->device_name = g_strdup(device_name);
}

/**
 * fwupd_report_get_distro_id:
 * @self: a #FwupdReport
 *
 * Gets the distribution name.
 *
 * Returns: the name, or %NULL if unset
 *
 * Since: 1.8.8
 **/
const gchar *
fwupd_report_get_distro_id(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->distro_id;
}

/**
 * fwupd_report_set_distro_id:
 * @self: a #FwupdReport
 * @distro_id: (nullable): the name, e.g. `fedora`
 *
 * Sets the distribution name.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_distro_id(FwupdReport *self, const gchar *distro_id)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));

	/* not changed */
	if (g_strcmp0(priv->distro_id, distro_id) == 0)
		return;

	g_free(priv->distro_id);
	priv->distro_id = g_strdup(distro_id);
}

/**
 * fwupd_report_get_distro_variant:
 * @self: a #FwupdReport
 *
 * Gets the distribution variant.
 *
 * Returns: variant, or %NULL if unset
 *
 * Since: 1.8.8
 **/
const gchar *
fwupd_report_get_distro_variant(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->distro_variant;
}

/**
 * fwupd_report_set_distro_variant:
 * @self: a #FwupdReport
 * @distro_variant: (nullable): the variant, e.g. `workstation`
 *
 * Sets the distribution variant.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_distro_variant(FwupdReport *self, const gchar *distro_variant)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));

	/* not changed */
	if (g_strcmp0(priv->distro_variant, distro_variant) == 0)
		return;

	g_free(priv->distro_variant);
	priv->distro_variant = g_strdup(distro_variant);
}

/**
 * fwupd_report_get_remote_id:
 * @self: a #FwupdReport
 *
 * Gets the remote ID.
 *
 * Returns: ID, or %NULL if unset
 *
 * Since: 1.9.3
 **/
const gchar *
fwupd_report_get_remote_id(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->remote_id;
}

/**
 * fwupd_report_set_remote_id:
 * @self: a #FwupdReport
 * @remote_id: (nullable): the remote, e.g. `lvfs`
 *
 * Sets the remote ID.
 *
 * Since: 1.9.3
 **/
void
fwupd_report_set_remote_id(FwupdReport *self, const gchar *remote_id)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));

	/* not changed */
	if (g_strcmp0(priv->remote_id, remote_id) == 0)
		return;

	g_free(priv->remote_id);
	priv->remote_id = g_strdup(remote_id);
}

/**
 * fwupd_report_get_distro_version:
 * @self: a #FwupdReport
 *
 * Gets the distribution version.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.8.8
 **/
const gchar *
fwupd_report_get_distro_version(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->distro_version;
}

/**
 * fwupd_report_set_distro_version:
 * @self: a #FwupdReport
 * @distro_version: (nullable): a string
 *
 * Sets the distribution version.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_set_distro_version(FwupdReport *self, const gchar *distro_version)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));

	/* not changed */
	if (g_strcmp0(priv->distro_version, distro_version) == 0)
		return;

	g_free(priv->distro_version);
	priv->distro_version = g_strdup(distro_version);
}

/**
 * fwupd_report_get_metadata:
 * @self: a #FwupdReport
 *
 * Gets the report metadata.
 *
 * Returns: (transfer none): the metadata, which may be empty
 *
 * Since: 1.8.8
 **/
GHashTable *
fwupd_report_get_metadata(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	return priv->metadata;
}

/**
 * fwupd_report_add_metadata_item:
 * @self: a #FwupdReport
 * @key: (not nullable): the key
 * @value: (not nullable): the value
 *
 * Sets a report metadata item.
 *
 * Since: 1.8.8
 **/
void
fwupd_report_add_metadata_item(FwupdReport *self, const gchar *key, const gchar *value)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_hash_table_insert(priv->metadata, g_strdup(key), g_strdup(value));
}

/**
 * fwupd_report_get_metadata_item:
 * @self: a #FwupdReport
 * @key: (not nullable): the key
 *
 * Gets a report metadata item.
 *
 * Returns: the value, or %NULL if unset
 *
 * Since: 1.8.8
 **/
const gchar *
fwupd_report_get_metadata_item(FwupdReport *self, const gchar *key)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	return g_hash_table_lookup(priv->metadata, key);
}

/**
 * fwupd_report_to_variant:
 * @self: a #FwupdReport
 *
 * Serialize the report data.
 *
 * Returns: the serialized data, or %NULL for error
 *
 * Since: 1.8.8
 **/
GVariant *
fwupd_report_to_variant(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;

	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->distro_id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DISTRO_ID,
				      g_variant_new_string(priv->distro_id));
	}
	if (priv->distro_variant != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DISTRO_VARIANT,
				      g_variant_new_string(priv->distro_variant));
	}
	if (priv->distro_version != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DISTRO_VERSION,
				      g_variant_new_string(priv->distro_version));
	}
	if (priv->vendor != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VENDOR,
				      g_variant_new_string(priv->vendor));
	}
	if (priv->device_name != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DEVICE_NAME,
				      g_variant_new_string(priv->device_name));
	}
	if (priv->created != 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CREATED,
				      g_variant_new_uint64(priv->created));
	}
	if (priv->version_old != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_OLD,
				      g_variant_new_string(priv->version_old));
	}
	if (priv->vendor_id > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VENDOR_ID,
				      g_variant_new_uint32(priv->vendor_id));
	}
	if (priv->remote_id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_REMOTE_ID,
				      g_variant_new_string(priv->remote_id));
	}
	if (g_hash_table_size(priv->metadata) > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_METADATA,
				      fwupd_hash_kv_to_variant(priv->metadata));
	}
	if (priv->flags > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FLAGS,
				      g_variant_new_uint64(priv->flags));
	}
	return g_variant_new("a{sv}", &builder);
}

static void
fwupd_report_from_key_value(FwupdReport *self, const gchar *key, GVariant *value)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DISTRO_ID) == 0) {
		fwupd_report_set_distro_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DISTRO_VARIANT) == 0) {
		fwupd_report_set_distro_variant(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DISTRO_VERSION) == 0) {
		fwupd_report_set_distro_version(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VENDOR) == 0) {
		fwupd_report_set_vendor(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VENDOR_ID) == 0) {
		fwupd_report_set_vendor_id(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DEVICE_NAME) == 0) {
		fwupd_report_set_device_name(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CREATED) == 0) {
		fwupd_report_set_created(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_OLD) == 0) {
		fwupd_report_set_version_old(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_REMOTE_ID) == 0) {
		fwupd_report_set_remote_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FLAGS) == 0) {
		fwupd_report_set_flags(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_METADATA) == 0) {
		g_hash_table_unref(priv->metadata);
		priv->metadata = fwupd_variant_to_hash_kv(value);
		return;
	}
}

/**
 * fwupd_report_to_json:
 * @self: a #FwupdReport
 * @builder: a JSON builder
 *
 * Adds a fwupd report to a JSON builder
 *
 * Since: 1.8.8
 **/
void
fwupd_report_to_json(FwupdReport *self, JsonBuilder *builder)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GList) keys = NULL;

	g_return_if_fail(FWUPD_IS_REPORT(self));
	g_return_if_fail(builder != NULL);

	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DEVICE_NAME, priv->device_name);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DISTRO_ID, priv->distro_id);
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_DISTRO_VARIANT,
				     priv->distro_variant);
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_DISTRO_VERSION,
				     priv->distro_version);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_VERSION_OLD, priv->version_old);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_REMOTE_ID, priv->remote_id);
	if (priv->vendor_id > 0)
		fwupd_common_json_add_int(builder, FWUPD_RESULT_KEY_VENDOR_ID, priv->vendor_id);

	if (priv->flags != FWUPD_REPORT_FLAG_NONE) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fwupd_report_flag_to_string((guint64)1 << i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}

	/* metadata */
	keys = g_hash_table_get_keys(priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(priv->metadata, key);
		fwupd_common_json_add_string(builder, key, value);
	}
}

static void
fwupd_pad_kv_dfl(GString *str, const gchar *key, guint64 report_flags)
{
	g_autoptr(GString) tmp = g_string_new("");
	for (guint i = 0; i < 64; i++) {
		if ((report_flags & ((guint64)1 << i)) == 0)
			continue;
		g_string_append_printf(tmp, "%s|", fwupd_report_flag_to_string((guint64)1 << i));
	}
	if (tmp->len == 0) {
		g_string_append(tmp, fwupd_report_flag_to_string(0));
	} else {
		g_string_truncate(tmp, tmp->len - 1);
	}
	fwupd_pad_kv_str(str, key, tmp->str);
}

/**
 * fwupd_report_to_string:
 * @self: a #FwupdReport
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.8.8
 **/
gchar *
fwupd_report_to_string(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	GString *str;
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail(FWUPD_IS_REPORT(self), NULL);

	str = g_string_new("");
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DEVICE_NAME, priv->device_name);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DISTRO_ID, priv->distro_id);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DISTRO_VARIANT, priv->distro_variant);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DISTRO_VERSION, priv->distro_version);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_VERSION_OLD, priv->version_old);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_VENDOR_ID, priv->vendor_id);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_REMOTE_ID, priv->remote_id);
	fwupd_pad_kv_dfl(str, FWUPD_RESULT_KEY_FLAGS, priv->flags);

	/* metadata */
	keys = g_hash_table_get_keys(priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(priv->metadata, key);
		fwupd_pad_kv_str(str, key, value);
	}

	return g_string_free(str, FALSE);
}

/**
 * fwupd_report_get_flags:
 * @self: a #FwupdReport
 *
 * Gets the report flags.
 *
 * Returns: report flags, or 0 if unset
 *
 * Since: 1.9.1
 **/
guint64
fwupd_report_get_flags(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), 0);
	return priv->flags;
}

/**
 * fwupd_report_set_flags:
 * @self: a #FwupdReport
 * @flags: report flags, e.g. %FWUPD_REPORT_FLAG_FROM_OEM
 *
 * Sets the report flags.
 *
 * Since: 1.9.1
 **/
void
fwupd_report_set_flags(FwupdReport *self, guint64 flags)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));
	if (priv->flags == flags)
		return;
	priv->flags = flags;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_report_add_flag:
 * @self: a #FwupdReport
 * @flag: the #FwupdReportFlags
 *
 * Adds a specific report flag to the report.
 *
 * Since: 1.9.1
 **/
void
fwupd_report_add_flag(FwupdReport *self, FwupdReportFlags flag)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));
	if (flag == 0)
		return;
	if ((priv->flags & flag) > 0)
		return;
	priv->flags |= flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_report_remove_flag:
 * @self: a #FwupdReport
 * @flag: a report flag
 *
 * Removes a specific report flag from the report.
 *
 * Since: 1.9.1
 **/
void
fwupd_report_remove_flag(FwupdReport *self, FwupdReportFlags flag)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REPORT(self));
	if (flag == 0)
		return;
	if ((priv->flags & flag) == 0)
		return;
	priv->flags &= ~flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_report_has_flag:
 * @self: a #FwupdReport
 * @flag: a report flag
 *
 * Finds if the report has a specific report flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.9.1
 **/
gboolean
fwupd_report_has_flag(FwupdReport *self, FwupdReportFlags flag)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REPORT(self), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fwupd_report_flag_to_string:
 * @report_flag: report flags, e.g. %FWUPD_REPORT_FLAG_FROM_OEM
 *
 * Converts an enumerated report flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.9.1
 **/
const gchar *
fwupd_report_flag_to_string(FwupdReportFlags report_flag)
{
	if (report_flag == FWUPD_REPORT_FLAG_NONE)
		return "none";
	if (report_flag == FWUPD_REPORT_FLAG_FROM_OEM)
		return "from-oem";
	return NULL;
}

/**
 * fwupd_report_flag_from_string:
 * @report_flag: (nullable): a string, e.g. `from-oem`
 *
 * Converts a string to an enumerated report flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.9.1
 **/
FwupdReportFlags
fwupd_report_flag_from_string(const gchar *report_flag)
{
	if (g_strcmp0(report_flag, "none") == 0)
		return FWUPD_REPORT_FLAG_NONE;
	if (g_strcmp0(report_flag, "from-oem") == 0)
		return FWUPD_REPORT_FLAG_FROM_OEM;
	return FWUPD_REPORT_FLAG_UNKNOWN;
}

static void
fwupd_report_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdReport *self = FWUPD_REPORT(object);
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FLAGS:
		g_value_set_uint64(value, priv->flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_report_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FwupdReport *self = FWUPD_REPORT(object);
	switch (prop_id) {
	case PROP_FLAGS:
		fwupd_report_set_flags(self, g_value_get_uint64(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_report_init(FwupdReport *self)
{
	FwupdReportPrivate *priv = GET_PRIVATE(self);
	priv->metadata = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
fwupd_report_finalize(GObject *object)
{
	FwupdReport *self = FWUPD_REPORT(object);
	FwupdReportPrivate *priv = GET_PRIVATE(self);

	g_free(priv->vendor);
	g_free(priv->device_name);
	g_free(priv->distro_id);
	g_free(priv->distro_version);
	g_free(priv->distro_variant);
	g_free(priv->remote_id);
	g_free(priv->version_old);
	g_hash_table_unref(priv->metadata);

	G_OBJECT_CLASS(fwupd_report_parent_class)->finalize(object);
}

static void
fwupd_report_class_init(FwupdReportClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fwupd_report_finalize;
	object_class->get_property = fwupd_report_get_property;
	object_class->set_property = fwupd_report_set_property;

	/**
	 * FwupdReport:flags:
	 *
	 * The report flags.
	 *
	 * Since: 1.9.1
	 */
	pspec = g_param_spec_uint64("flags",
				    NULL,
				    NULL,
				    FWUPD_REPORT_FLAG_NONE,
				    FWUPD_REPORT_FLAG_UNKNOWN,
				    FWUPD_REPORT_FLAG_NONE,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLAGS, pspec);
}

static void
fwupd_report_set_from_variant_iter(FwupdReport *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_report_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

/**
 * fwupd_report_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new report using serialized data.
 *
 * Returns: (transfer full): a new #FwupdReport, or %NULL if @value was invalid
 *
 * Since: 1.8.8
 **/
FwupdReport *
fwupd_report_from_variant(GVariant *value)
{
	FwupdReport *self = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string(value);
	if (g_strcmp0(type_string, "(a{sv})") == 0) {
		self = fwupd_report_new();
		g_variant_get(value, "(a{sv})", &iter);
		fwupd_report_set_from_variant_iter(self, iter);
	} else if (g_strcmp0(type_string, "a{sv}") == 0) {
		self = fwupd_report_new();
		g_variant_get(value, "a{sv}", &iter);
		fwupd_report_set_from_variant_iter(self, iter);
	} else {
		g_warning("type %s not known", type_string);
	}
	return self;
}

/**
 * fwupd_report_new:
 *
 * Creates a new report.
 *
 * Returns: a new #FwupdReport
 *
 * Since: 1.8.8
 **/
FwupdReport *
fwupd_report_new(void)
{
	FwupdReport *self;
	self = g_object_new(FWUPD_TYPE_REPORT, NULL);
	return FWUPD_REPORT(self);
}
