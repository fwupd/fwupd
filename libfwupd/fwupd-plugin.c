/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fwupd-enums-private.h"
#include "fwupd-plugin-private.h"

/**
 * SECTION:fwupd-plugin
 * @short_description: a hardware plugin
 *
 * An object that represents a fwupd plugin.
 *
 * See also: #FwupdRelease
 */

static void fwupd_plugin_finalize	 (GObject *object);

typedef struct {
	gchar				*name;
	guint64				 flags;
} FwupdPluginPrivate;

enum {
	PROP_0,
	PROP_NAME,
	PROP_FLAGS,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (FwupdPlugin, fwupd_plugin, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_plugin_get_instance_private (o))

/**
 * fwupd_plugin_get_name:
 * @plugin: A #FwupdPlugin
 *
 * Gets the plugin name.
 *
 * Returns: the plugin name, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_plugin_get_name (FwupdPlugin *plugin)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FWUPD_IS_PLUGIN (plugin), NULL);
	return priv->name;
}

/**
 * fwupd_plugin_set_name:
 * @plugin: A #FwupdPlugin
 * @name: the plugin name, e.g. `bios`
 *
 * Sets the plugin name.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_set_name (FwupdPlugin *plugin, const gchar *name)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FWUPD_IS_PLUGIN (plugin));
	g_return_if_fail (name != NULL);

	/* not changed */
	if (g_strcmp0 (priv->name, name) == 0)
		return;

	g_free (priv->name);
	priv->name = g_strdup (name);
	g_object_notify (G_OBJECT (plugin), "name");
}

/**
 * fwupd_plugin_get_flags:
 * @plugin: A #FwupdPlugin
 *
 * Gets the plugin flags.
 *
 * Returns: the plugin flags, or 0 if unset
 *
 * Since: 1.5.0
 **/
guint64
fwupd_plugin_get_flags (FwupdPlugin *plugin)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FWUPD_IS_PLUGIN (plugin), 0);
	return priv->flags;
}

/**
 * fwupd_plugin_set_flags:
 * @plugin: A #FwupdPlugin
 * @flags: the plugin flags, e.g. %FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED
 *
 * Sets the plugin flags.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_set_flags (FwupdPlugin *plugin, guint64 flags)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FWUPD_IS_PLUGIN (plugin));
	if (priv->flags == flags)
		return;
	priv->flags = flags;
	g_object_notify (G_OBJECT (plugin), "flags");
}

/**
 * fwupd_plugin_add_flag:
 * @plugin: A #FwupdPlugin
 * @flag: the #FwupdPluginFlags
 *
 * Adds a specific plugin flag to the plugin.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_add_flag (FwupdPlugin *plugin, FwupdPluginFlags flag)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FWUPD_IS_PLUGIN (plugin));
	if (flag == 0)
		return;
	if ((priv->flags & flag) > 0)
		return;
	priv->flags |= flag;
	g_object_notify (G_OBJECT (plugin), "flags");
}

/**
 * fwupd_plugin_remove_flag:
 * @plugin: A #FwupdPlugin
 * @flag: the #FwupdPluginFlags
 *
 * Removes a specific plugin flag from the plugin.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_remove_flag (FwupdPlugin *plugin, FwupdPluginFlags flag)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FWUPD_IS_PLUGIN (plugin));
	if (flag == 0)
		return;
	if ((priv->flags & flag) == 0)
		return;
	priv->flags &= ~flag;
	g_object_notify (G_OBJECT (plugin), "flags");
}

/**
 * fwupd_plugin_has_flag:
 * @plugin: A #FwupdPlugin
 * @flag: the #FwupdPluginFlags
 *
 * Finds if the plugin has a specific plugin flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_plugin_has_flag (FwupdPlugin *plugin, FwupdPluginFlags flag)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FWUPD_IS_PLUGIN (plugin), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fwupd_plugin_to_variant:
 * @plugin: A #FwupdPlugin
 *
 * Creates a GVariant from the plugin data omitting sensitive fields
 *
 * Returns: the GVariant, or %NULL for error
 *
 * Since: 1.5.0
 **/
GVariant *
fwupd_plugin_to_variant (FwupdPlugin *plugin)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	GVariantBuilder builder;

	g_return_val_if_fail (FWUPD_IS_PLUGIN (plugin), NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->name != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_NAME,
				       g_variant_new_string (priv->name));
	}
	if (priv->flags > 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_FLAGS,
				       g_variant_new_uint64 (priv->flags));
	}
	return g_variant_new ("a{sv}", &builder);
}

static void
fwupd_plugin_from_key_value (FwupdPlugin *plugin, const gchar *key, GVariant *value)
{
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_plugin_set_name (plugin, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_FLAGS) == 0) {
		fwupd_plugin_set_flags (plugin, g_variant_get_uint64 (value));
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
fwupd_pad_kv_dfl (GString *str, const gchar *key, guint64 plugin_flags)
{
	g_autoptr(GString) tmp = g_string_new ("");
	for (guint i = 0; i < 64; i++) {
		if ((plugin_flags & ((guint64) 1 << i)) == 0)
			continue;
		g_string_append_printf (tmp, "%s|",
					fwupd_plugin_flag_to_string ((guint64) 1 << i));
	}
	if (tmp->len == 0) {
		g_string_append (tmp, fwupd_plugin_flag_to_string (0));
	} else {
		g_string_truncate (tmp, tmp->len - 1);
	}
	fwupd_pad_kv_str (str, key, tmp->str);
}

static void
fwupd_plugin_json_add_string (JsonBuilder *builder, const gchar *key, const gchar *str)
{
	if (str == NULL)
		return;
	json_builder_set_member_name (builder, key);
	json_builder_add_string_value (builder, str);
}

/**
 * fwupd_plugin_to_json:
 * @plugin: A #FwupdPlugin
 * @builder: A #JsonBuilder
 *
 * Adds a fwupd plugin to a JSON builder
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_to_json (FwupdPlugin *plugin, JsonBuilder *builder)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);

	g_return_if_fail (FWUPD_IS_PLUGIN (plugin));
	g_return_if_fail (builder != NULL);

	fwupd_plugin_json_add_string (builder, FWUPD_RESULT_KEY_NAME, priv->name);
	if (priv->flags != FWUPD_PLUGIN_FLAG_NONE) {
		json_builder_set_member_name (builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array (builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64) 1 << i)) == 0)
				continue;
			tmp = fwupd_plugin_flag_to_string ((guint64) 1 << i);
			json_builder_add_string_value (builder, tmp);
		}
		json_builder_end_array (builder);
	}
}

/**
 * fwupd_plugin_to_string:
 * @plugin: A #FwupdPlugin
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.5.0
 **/
gchar *
fwupd_plugin_to_string (FwupdPlugin *plugin)
{
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	GString *str;

	g_return_val_if_fail (FWUPD_IS_PLUGIN (plugin), NULL);

	str = g_string_new (NULL);
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_pad_kv_dfl (str, FWUPD_RESULT_KEY_FLAGS, priv->flags);
	return g_string_free (str, FALSE);
}

static void
fwupd_plugin_get_property (GObject *object, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdPlugin *self = FWUPD_PLUGIN (object);
	FwupdPluginPrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_FLAGS:
		g_value_set_uint64 (value, priv->flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_plugin_set_property (GObject *object, guint prop_id,
			   const GValue *value, GParamSpec *pspec)
{
	FwupdPlugin *self = FWUPD_PLUGIN (object);
	switch (prop_id) {
	case PROP_NAME:
		fwupd_plugin_set_name (self, g_value_get_string (value));
		break;
	case PROP_FLAGS:
		fwupd_plugin_set_flags (self, g_value_get_uint64 (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_plugin_class_init (FwupdPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = fwupd_plugin_finalize;
	object_class->get_property = fwupd_plugin_get_property;
	object_class->set_property = fwupd_plugin_set_property;

	pspec = g_param_spec_string ("name", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	pspec = g_param_spec_uint64 ("flags", NULL, NULL,
				     FWUPD_PLUGIN_FLAG_NONE,
				     FWUPD_PLUGIN_FLAG_UNKNOWN,
				     FWUPD_PLUGIN_FLAG_NONE,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_FLAGS, pspec);
}

static void
fwupd_plugin_init (FwupdPlugin *plugin)
{
}

static void
fwupd_plugin_finalize (GObject *object)
{
	FwupdPlugin *plugin = FWUPD_PLUGIN (object);
	FwupdPluginPrivate *priv = GET_PRIVATE (plugin);
	g_free (priv->name);
	G_OBJECT_CLASS (fwupd_plugin_parent_class)->finalize (object);
}

static void
fwupd_plugin_set_from_variant_iter (FwupdPlugin *plugin, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next (iter, "{&sv}", &key, &value)) {
		fwupd_plugin_from_key_value (plugin, key, value);
		g_variant_unref (value);
	}
}

/**
 * fwupd_plugin_from_variant:
 * @value: a #GVariant
 *
 * Creates a new plugin using packed data.
 *
 * Returns: (transfer full): a new #FwupdPlugin, or %NULL if @value was invalid
 *
 * Since: 1.5.0
 **/
FwupdPlugin *
fwupd_plugin_from_variant (GVariant *value)
{
	FwupdPlugin *plugin = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string (value);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		plugin = fwupd_plugin_new ();
		g_variant_get (value, "(a{sv})", &iter);
		fwupd_plugin_set_from_variant_iter (plugin, iter);
	} else if (g_strcmp0 (type_string, "a{sv}") == 0) {
		plugin = fwupd_plugin_new ();
		g_variant_get (value, "a{sv}", &iter);
		fwupd_plugin_set_from_variant_iter (plugin, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return plugin;
}

/**
 * fwupd_plugin_array_from_variant:
 * @value: a #GVariant
 *
 * Creates an array of new plugins using packed data.
 *
 * Returns: (transfer container) (element-type FwupdPlugin): plugins, or %NULL if @value was invalid
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_plugin_array_from_variant (GVariant *value)
{
	GPtrArray *array = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	untuple = g_variant_get_child_value (value, 0);
	sz = g_variant_n_children (untuple);
	for (guint i = 0; i < sz; i++) {
		FwupdPlugin *plugin;
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value (untuple, i);
		plugin = fwupd_plugin_from_variant (data);
		if (plugin == NULL)
			continue;
		g_ptr_array_add (array, plugin);
	}
	return array;
}

/**
 * fwupd_plugin_new:
 *
 * Creates a new plugin.
 *
 * Returns: a new #FwupdPlugin
 *
 * Since: 1.5.0
 **/
FwupdPlugin *
fwupd_plugin_new (void)
{
	FwupdPlugin *plugin;
	plugin = g_object_new (FWUPD_TYPE_PLUGIN, NULL);
	return FWUPD_PLUGIN (plugin);
}
