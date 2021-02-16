/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-security-attr-private.h"

/**
 * SECTION:fwupd-security-attr
 *
 * An object that represents an Host Security ID attribute.
 */

static void fwupd_security_attr_finalize	 (GObject *object);

typedef struct {
	gchar				*appstream_id;
	GPtrArray			*obsoletes;
	GHashTable			*metadata;	/* (nullable) */
	gchar				*name;
	gchar				*plugin;
	gchar				*url;
	FwupdSecurityAttrLevel		 level;
	FwupdSecurityAttrResult		 result;
	FwupdSecurityAttrFlags		 flags;
} FwupdSecurityAttrPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FwupdSecurityAttr, fwupd_security_attr, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_security_attr_get_instance_private (o))

/**
 * fwupd_security_attr_flag_to_string:
 * @flag: A #FwupdSecurityAttrFlags, e.g. %FWUPD_SECURITY_ATTR_FLAG_SUCCESS
 *
 * Returns the printable string for the flag.
 *
 * Returns: string, or %NULL
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_flag_to_string (FwupdSecurityAttrFlags flag)
{
	if (flag == FWUPD_SECURITY_ATTR_FLAG_NONE)
		return "none";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_SUCCESS)
		return "success";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)
		return "obsoleted";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES)
		return "runtime-updates";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION)
		return "runtime-attestation";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)
		return "runtime-issue";
	return NULL;
}

/**
 * fwupd_security_attr_result_to_string:
 * @result: A #FwupdSecurityAttrResult, e.g. %FWUPD_SECURITY_ATTR_RESULT_ENABLED
 *
 * Returns the printable string for the result enum.
 *
 * Returns: string, or %NULL
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_result_to_string (FwupdSecurityAttrResult result)
{
	if (result == FWUPD_SECURITY_ATTR_RESULT_VALID)
		return "valid";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_VALID)
		return "not-valid";
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENABLED)
		return "enabled";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED)
		return "not-enabled";
	if (result == FWUPD_SECURITY_ATTR_RESULT_LOCKED)
		return "locked";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED)
		return "not-locked";
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED)
		return "encrypted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED)
		return "not-encrypted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_TAINTED)
		return "tainted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED)
		return "not-tainted";
	if (result == FWUPD_SECURITY_ATTR_RESULT_FOUND)
		return "found";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND)
		return "not-found";
	if (result == FWUPD_SECURITY_ATTR_RESULT_SUPPORTED)
		return "supported";
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED)
		return "not-supported";
	return NULL;
}

/**
 * fwupd_security_attr_flag_to_suffix:
 * @flag: A #FwupdSecurityAttrFlags, e.g. %FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES
 *
 * Returns the string suffix for the flag.
 *
 * Returns: string, or %NULL
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_flag_to_suffix (FwupdSecurityAttrFlags flag)
{
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES)
		return "U";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION)
		return "A";
	if (flag == FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)
		return "!";
	return NULL;
}

/**
 * fwupd_security_attr_get_obsoletes:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the list of attribute obsoletes. The obsoleted attributes will not
 * contribute to the calculated HSI value or be visible in command line tools.
 *
 * Returns: (element-type utf8) (transfer none): the obsoletes, which may be empty
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_security_attr_get_obsoletes (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);
	return priv->obsoletes;
}

/**
 * fwupd_security_attr_add_obsolete:
 * @self: A #FwupdSecurityAttr
 * @appstream_id: the appstream_id or plugin name
 *
 * Adds an attribute appstream_id to obsolete. The obsoleted attribute will not
 * contribute to the calculated HSI value or be visible in command line tools.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_add_obsolete (FwupdSecurityAttr *self, const gchar *appstream_id)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));
	g_return_if_fail (appstream_id != NULL);
	if (fwupd_security_attr_has_obsolete (self, appstream_id))
		return;
	g_ptr_array_add (priv->obsoletes, g_strdup (appstream_id));
}

/**
 * fwupd_security_attr_has_obsolete:
 * @self: A #FwupdSecurityAttr
 * @appstream_id: the attribute appstream_id
 *
 * Finds out if the attribute obsoletes a specific appstream_id.
 *
 * Returns: %TRUE if the self matches
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_security_attr_has_obsolete (FwupdSecurityAttr *self, const gchar *appstream_id)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), FALSE);
	g_return_val_if_fail (appstream_id != NULL, FALSE);
	for (guint i = 0; i < priv->obsoletes->len; i++) {
		const gchar *obsolete_tmp = g_ptr_array_index (priv->obsoletes, i);
		if (g_strcmp0 (obsolete_tmp, appstream_id) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_security_attr_get_appstream_id:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the AppStream ID.
 *
 * Returns: the AppStream ID, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_appstream_id (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);
	return priv->appstream_id;
}

/**
 * fwupd_security_attr_set_appstream_id:
 * @self: A #FwupdSecurityAttr
 * @appstream_id: the AppStream component ID, e.g. `com.intel.BiosGuard`
 *
 * Sets the AppStream ID.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_appstream_id (FwupdSecurityAttr *self, const gchar *appstream_id)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));

	/* not changed */
	if (g_strcmp0 (priv->appstream_id, appstream_id) == 0)
		return;

	/* sanity check */
	if (!g_str_has_prefix (appstream_id, "org.fwupd.hsi."))
		g_critical ("HSI attributes need to have a 'org.fwupd.hsi.' prefix");

	g_free (priv->appstream_id);
	priv->appstream_id = g_strdup (appstream_id);
}

/**
 * fwupd_security_attr_get_url:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the attribute URL.
 *
 * Returns: the attribute result, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_url (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);
	return priv->url;
}

/**
 * fwupd_security_attr_set_name:
 * @self: A #FwupdSecurityAttr
 * @name: the attribute name
 *
 * Sets the attribute name.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_name (FwupdSecurityAttr *self, const gchar *name)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));

	/* not changed */
	if (g_strcmp0 (priv->name, name) == 0)
		return;

	g_free (priv->name);
	priv->name = g_strdup (name);
}

/**
 * fwupd_security_attr_set_plugin:
 * @self: A #FwupdSecurityAttr
 * @plugin: the plugin name
 *
 * Sets the plugin that created the attribute.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_plugin (FwupdSecurityAttr *self, const gchar *plugin)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));

	/* not changed */
	if (g_strcmp0 (priv->plugin, plugin) == 0)
		return;

	g_free (priv->plugin);
	priv->plugin = g_strdup (plugin);
}

/**
 * fwupd_security_attr_set_url:
 * @self: A #FwupdSecurityAttr
 * @url: the attribute URL
 *
 * Sets the attribute result.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_url (FwupdSecurityAttr *self, const gchar *url)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));

	/* not changed */
	if (g_strcmp0 (priv->url, url) == 0)
		return;

	g_free (priv->url);
	priv->url = g_strdup (url);
}

/**
 * fwupd_security_attr_get_name:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the attribute name.
 *
 * Returns: the attribute name, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_name (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);
	return priv->name;
}

/**
 * fwupd_security_attr_get_plugin:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the plugin that created the attribute.
 *
 * Returns: the plugin name, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_plugin (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);
	return priv->plugin;
}

/**
 * fwupd_security_attr_get_flags:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the self flags.
 *
 * Returns: the self flags, or 0 if unset
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttrFlags
fwupd_security_attr_get_flags (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), 0);
	return priv->flags;
}

/**
 * fwupd_security_attr_set_flags:
 * @self: A #FwupdSecurityAttr
 * @flags: the self flags, e.g. %FWUPD_SECURITY_ATTR_FLAG_OBSOLETED
 *
 * Sets the self flags.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_flags (FwupdSecurityAttr *self, FwupdSecurityAttrFlags flags)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));
	priv->flags = flags;
}

/**
 * fwupd_security_attr_add_flag:
 * @self: A #FwupdSecurityAttr
 * @flag: the #FwupdSecurityAttrFlags
 *
 * Adds a specific self flag to the self.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_add_flag (FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));
	priv->flags |= flag;
}

/**
 * fwupd_security_attr_has_flag:
 * @self: A #FwupdSecurityAttr
 * @flag: the #FwupdSecurityAttrFlags
 *
 * Finds if the self has a specific self flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_security_attr_has_flag (FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fwupd_security_attr_get_level:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the HSI level.
 *
 * Returns: the #FwupdSecurityAttrLevel, or %FWUPD_SECURITY_ATTR_LEVEL_NONE if unset
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttrLevel
fwupd_security_attr_get_level (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), 0);
	return priv->level;
}

/**
 * fwupd_security_attr_set_level:
 * @self: A #FwupdSecurityAttr
 * @level: A #FwupdSecurityAttrLevel, e.g. %FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT
 *
 * Sets the HSI level. A @level of %FWUPD_SECURITY_ATTR_LEVEL_NONE is not used
 * for the HSI calculation.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_level (FwupdSecurityAttr *self, FwupdSecurityAttrLevel level)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));
	priv->level = level;
}

/**
 * fwupd_security_attr_set_result:
 * @self: A #FwupdSecurityAttr
 * @result: A #FwupdSecurityAttrResult, e.g. %FWUPD_SECURITY_ATTR_LEVEL_LOCKED
 *
 * Sets the optional HSI result. This is required because some attributes may
 * be a "success" when something is `locked` or may be "failed" if `found`.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_set_result (FwupdSecurityAttr *self, FwupdSecurityAttrResult result)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));
	priv->result = result;
}

/**
 * fwupd_security_attr_get_result:
 * @self: A #FwupdSecurityAttr
 *
 * Gets the optional HSI result.
 *
 * Returns: the #FwupdSecurityAttrResult, e.g %FWUPD_SECURITY_ATTR_LEVEL_LOCKED
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttrResult
fwupd_security_attr_get_result (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), 0);
	return priv->result;
}

/**
 * fwupd_security_attr_to_variant:
 * @self: A #FwupdSecurityAttr
 *
 * Creates a GVariant from the self data.
 *
 * Returns: the GVariant, or %NULL for error
 *
 * Since: 1.5.0
 **/
GVariant *
fwupd_security_attr_to_variant (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	GVariantBuilder builder;

	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);

	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->appstream_id != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_APPSTREAM_ID,
				       g_variant_new_string (priv->appstream_id));
	}
	if (priv->name != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_NAME,
				       g_variant_new_string (priv->name));
	}
	if (priv->url != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_URI,
				       g_variant_new_string (priv->url));
	}
	if (priv->obsoletes->len > 0) {
		g_autofree const gchar **strv = g_new0 (const gchar *, priv->obsoletes->len + 1);
		for (guint i = 0; i < priv->obsoletes->len; i++)
			strv[i] = (const gchar *) g_ptr_array_index (priv->obsoletes, i);
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_CATEGORIES,
				       g_variant_new_strv (strv, -1));
	}
	if (priv->flags != 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_FLAGS,
				       g_variant_new_uint64 (priv->flags));
	}
	if (priv->level > 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_HSI_LEVEL,
				       g_variant_new_uint32 (priv->level));
	}
	if (priv->result != FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_HSI_RESULT,
				       g_variant_new_uint32 (priv->result));
	}
	if (priv->metadata != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_METADATA,
				       fwupd_hash_kv_to_variant (priv->metadata));
	}
	return g_variant_new ("a{sv}", &builder);
}

/**
 * fwupd_security_attr_get_metadata:
 * @self: A #FwupdSecurityAttr
 * @key: metadata key
 *
 * Gets private metadata from the attribute which may be used in the name.
 *
 * Returns: (nullable): the metadata value, or %NULL if unfound
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_security_attr_get_metadata (FwupdSecurityAttr *self, const gchar *key)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	if (priv->metadata == NULL)
		return NULL;
	return g_hash_table_lookup (priv->metadata, key);
}

/**
 * fwupd_security_attr_add_metadata:
 * @self: A #FwupdSecurityAttr
 * @key: metadata key
 * @value: (nullable): metadata value
 *
 * Adds metadata to the attribute which may be used in the name.
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_add_metadata (FwupdSecurityAttr *self,
				  const gchar *key,
				  const gchar *value)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));
	g_return_if_fail (key != NULL);

	if (priv->metadata == NULL) {
		priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
							g_free, g_free);
	}
	g_hash_table_insert (priv->metadata, g_strdup (key), g_strdup (value));
}

static void
fwupd_security_attr_from_key_value (FwupdSecurityAttr *self, const gchar *key, GVariant *value)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);

	if (g_strcmp0 (key, FWUPD_RESULT_KEY_APPSTREAM_ID) == 0) {
		fwupd_security_attr_set_appstream_id (self, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_security_attr_set_name (self, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_URI) == 0) {
		fwupd_security_attr_set_url (self, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_FLAGS) == 0) {
		fwupd_security_attr_set_flags (self, g_variant_get_uint64 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_HSI_LEVEL) == 0) {
		fwupd_security_attr_set_level (self, g_variant_get_uint32 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_HSI_RESULT) == 0) {
		fwupd_security_attr_set_result (self, g_variant_get_uint32 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_METADATA) == 0) {
		if (priv->metadata != NULL)
			g_hash_table_unref (priv->metadata);
		priv->metadata = fwupd_variant_to_hash_kv (value);
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
fwupd_pad_kv_tfl (GString *str, const gchar *key, FwupdSecurityAttrFlags security_attr_flags)
{
	g_autoptr(GString) tmp = g_string_new ("");
	for (guint i = 0; i < 64; i++) {
		if ((security_attr_flags & ((guint64) 1 << i)) == 0)
			continue;
		g_string_append_printf (tmp, "%s|",
					fwupd_security_attr_flag_to_string ((guint64) 1 << i));
	}
	if (tmp->len == 0) {
		g_string_append (tmp, fwupd_security_attr_flag_to_string (0));
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
fwupd_security_attr_json_add_string (JsonBuilder *builder, const gchar *key, const gchar *str)
{
	if (str == NULL)
		return;
	json_builder_set_member_name (builder, key);
	json_builder_add_string_value (builder, str);
}

static void
fwupd_security_attr_json_add_int (JsonBuilder *builder, const gchar *key, guint64 num)
{
	if (num == 0)
		return;
	json_builder_set_member_name (builder, key);
	json_builder_add_int_value (builder, num);
}

/**
 * fwupd_security_attr_to_json:
 * @self: A #FwupdSecurityAttr
 * @builder: A #JsonBuilder
 *
 * Adds a fwupd self to a JSON builder
 *
 * Since: 1.5.0
 **/
void
fwupd_security_attr_to_json (FwupdSecurityAttr *self, JsonBuilder *builder)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (self));
	g_return_if_fail (builder != NULL);

	fwupd_security_attr_json_add_string (builder, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	fwupd_security_attr_json_add_int (builder, FWUPD_RESULT_KEY_HSI_LEVEL, priv->level);
	fwupd_security_attr_json_add_string (builder, FWUPD_RESULT_KEY_HSI_RESULT,
					     fwupd_security_attr_result_to_string (priv->result));
	fwupd_security_attr_json_add_string (builder, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_security_attr_json_add_string (builder, FWUPD_RESULT_KEY_PLUGIN, priv->plugin);
	fwupd_security_attr_json_add_string (builder, FWUPD_RESULT_KEY_URI, priv->url);
	if (priv->flags != FWUPD_SECURITY_ATTR_FLAG_NONE) {
		json_builder_set_member_name (builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array (builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64) 1 << i)) == 0)
				continue;
			tmp = fwupd_security_attr_flag_to_string ((guint64) 1 << i);
			json_builder_add_string_value (builder, tmp);
		}
		json_builder_end_array (builder);
	}
	if (priv->metadata != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys (priv->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			const gchar *value = g_hash_table_lookup (priv->metadata, key);
			fwupd_security_attr_json_add_string (builder, key, value);
		}
	}
}

/**
 * fwupd_security_attr_to_string:
 * @self: A #FwupdSecurityAttr
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.5.0
 **/
gchar *
fwupd_security_attr_to_string (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	GString *str;

	g_return_val_if_fail (FWUPD_IS_SECURITY_ATTR (self), NULL);

	str = g_string_new ("");
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->appstream_id);
	fwupd_pad_kv_int (str, FWUPD_RESULT_KEY_HSI_LEVEL, priv->level);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_HSI_RESULT,
			  fwupd_security_attr_result_to_string (priv->result));
	if (priv->flags != FWUPD_SECURITY_ATTR_FLAG_NONE)
		fwupd_pad_kv_tfl (str, FWUPD_RESULT_KEY_FLAGS, priv->flags);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_PLUGIN, priv->plugin);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_URI, priv->url);
	for (guint i = 0; i < priv->obsoletes->len; i++) {
		const gchar *appstream_id = g_ptr_array_index (priv->obsoletes, i);
		fwupd_pad_kv_str (str, "Obsolete", appstream_id);
	}
	if (priv->metadata != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys (priv->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			const gchar *value = g_hash_table_lookup (priv->metadata, key);
			fwupd_pad_kv_str (str, key, value);
		}
	}

	return g_string_free (str, FALSE);
}

static void
fwupd_security_attr_class_init (FwupdSecurityAttrClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fwupd_security_attr_finalize;
}

static void
fwupd_security_attr_init (FwupdSecurityAttr *self)
{
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);
	priv->obsoletes = g_ptr_array_new_with_free_func (g_free);
}

static void
fwupd_security_attr_finalize (GObject *object)
{
	FwupdSecurityAttr *self = FWUPD_SECURITY_ATTR (object);
	FwupdSecurityAttrPrivate *priv = GET_PRIVATE (self);

	if (priv->metadata != NULL)
		g_hash_table_unref (priv->metadata);
	g_free (priv->appstream_id);
	g_free (priv->name);
	g_free (priv->plugin);
	g_free (priv->url);
	g_ptr_array_unref (priv->obsoletes);

	G_OBJECT_CLASS (fwupd_security_attr_parent_class)->finalize (object);
}

static void
fwupd_security_attr_set_from_variant_iter (FwupdSecurityAttr *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next (iter, "{&sv}", &key, &value)) {
		fwupd_security_attr_from_key_value (self, key, value);
		g_variant_unref (value);
	}
}

/**
 * fwupd_security_attr_from_variant:
 * @value: a #GVariant
 *
 * Creates a new self using packed data.
 *
 * Returns: (transfer full): a new #FwupdSecurityAttr, or %NULL if @value was invalid
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttr *
fwupd_security_attr_from_variant (GVariant *value)
{
	FwupdSecurityAttr *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	type_string = g_variant_get_type_string (value);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		rel = fwupd_security_attr_new (NULL);
		g_variant_get (value, "(a{sv})", &iter);
		fwupd_security_attr_set_from_variant_iter (rel, iter);
	} else if (g_strcmp0 (type_string, "a{sv}") == 0) {
		rel = fwupd_security_attr_new (NULL);
		g_variant_get (value, "a{sv}", &iter);
		fwupd_security_attr_set_from_variant_iter (rel, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return rel;
}

/**
 * fwupd_security_attr_array_from_variant:
 * @value: a #GVariant
 *
 * Creates an array of new security_attrs using packed data.
 *
 * Returns: (transfer container) (element-type FwupdSecurityAttr): attributes, or %NULL if @value was invalid
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_security_attr_array_from_variant (GVariant *value)
{
	GPtrArray *array = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	untuple = g_variant_get_child_value (value, 0);
	sz = g_variant_n_children (untuple);
	for (guint i = 0; i < sz; i++) {
		FwupdSecurityAttr *rel;
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value (untuple, i);
		rel = fwupd_security_attr_from_variant (data);
		if (rel == NULL)
			continue;
		g_ptr_array_add (array, rel);
	}
	return array;
}

/**
 * fwupd_security_attr_new:
 * @appstream_id: (allow-none): the AppStream component ID, e.g. `com.intel.BiosGuard`
 *
 * Creates a new self.
 *
 * Returns: a new #FwupdSecurityAttr
 *
 * Since: 1.5.0
 **/
FwupdSecurityAttr *
fwupd_security_attr_new (const gchar *appstream_id)
{
	FwupdSecurityAttr *self;
	self = g_object_new (FWUPD_TYPE_SECURITY_ATTR, NULL);
	if (appstream_id != NULL)
		fwupd_security_attr_set_appstream_id (self, appstream_id);
	return FWUPD_SECURITY_ATTR (self);
}
