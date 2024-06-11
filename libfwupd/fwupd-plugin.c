/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fwupd-codec.h"
#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-plugin.h"

/**
 * FwupdPlugin:
 *
 * A plugin which is used by fwupd to enumerate and update devices.
 *
 * See also: [class@FwupdRelease]
 */

static void
fwupd_plugin_finalize(GObject *object);

typedef struct {
	gchar *name;
	guint64 flags;
} FwupdPluginPrivate;

enum { PROP_0, PROP_NAME, PROP_FLAGS, PROP_LAST };

static void
fwupd_plugin_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FwupdPlugin,
		       fwupd_plugin,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FwupdPlugin)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fwupd_plugin_codec_iface_init));

#define GET_PRIVATE(o) (fwupd_plugin_get_instance_private(o))

/**
 * fwupd_plugin_get_name:
 * @self: a #FwupdPlugin
 *
 * Gets the plugin name.
 *
 * Returns: the plugin name, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_plugin_get_name(FwupdPlugin *self)
{
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_PLUGIN(self), NULL);
	return priv->name;
}

/**
 * fwupd_plugin_set_name:
 * @self: a #FwupdPlugin
 * @name: the plugin name, e.g. `bios`
 *
 * Sets the plugin name.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_set_name(FwupdPlugin *self, const gchar *name)
{
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_PLUGIN(self));
	g_return_if_fail(name != NULL);

	/* not changed */
	if (g_strcmp0(priv->name, name) == 0)
		return;

	g_free(priv->name);
	priv->name = g_strdup(name);
	g_object_notify(G_OBJECT(self), "name");
}

/**
 * fwupd_plugin_get_flags:
 * @self: a #FwupdPlugin
 *
 * Gets the plugin flags.
 *
 * Returns: plugin flags, or 0 if unset
 *
 * Since: 1.5.0
 **/
guint64
fwupd_plugin_get_flags(FwupdPlugin *self)
{
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_PLUGIN(self), 0);
	return priv->flags;
}

/**
 * fwupd_plugin_set_flags:
 * @self: a #FwupdPlugin
 * @flags: plugin flags, e.g. %FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED
 *
 * Sets the plugin flags.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_set_flags(FwupdPlugin *self, guint64 flags)
{
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_PLUGIN(self));
	if (priv->flags == flags)
		return;
	priv->flags = flags;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_plugin_add_flag:
 * @self: a #FwupdPlugin
 * @flag: the #FwupdPluginFlags
 *
 * Adds a specific plugin flag to the plugin.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_add_flag(FwupdPlugin *self, FwupdPluginFlags flag)
{
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_PLUGIN(self));
	if (flag == 0)
		return;
	if ((priv->flags & flag) > 0)
		return;
	priv->flags |= flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_plugin_remove_flag:
 * @self: a #FwupdPlugin
 * @flag: a plugin flag
 *
 * Removes a specific plugin flag from the plugin.
 *
 * Since: 1.5.0
 **/
void
fwupd_plugin_remove_flag(FwupdPlugin *self, FwupdPluginFlags flag)
{
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_PLUGIN(self));
	if (flag == 0)
		return;
	if ((priv->flags & flag) == 0)
		return;
	priv->flags &= ~flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_plugin_has_flag:
 * @self: a #FwupdPlugin
 * @flag: a plugin flag
 *
 * Finds if the plugin has a specific plugin flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_plugin_has_flag(FwupdPlugin *self, FwupdPluginFlags flag)
{
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_PLUGIN(self), FALSE);
	return (priv->flags & flag) > 0;
}

static void
fwupd_plugin_add_variant(FwupdCodec *codec, GVariantBuilder *builder, FwupdCodecFlags flags)
{
	FwupdPlugin *self = FWUPD_PLUGIN(codec);
	FwupdPluginPrivate *priv = GET_PRIVATE(self);

	if (priv->name != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_NAME,
				      g_variant_new_string(priv->name));
	}
	if (priv->flags > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FLAGS,
				      g_variant_new_uint64(priv->flags));
	}
}

static void
fwupd_plugin_from_key_value(FwupdPlugin *self, const gchar *key, GVariant *value)
{
	if (g_strcmp0(key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_plugin_set_name(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FLAGS) == 0) {
		fwupd_plugin_set_flags(self, g_variant_get_uint64(value));
		return;
	}
}

static void
fwupd_plugin_string_append_flags(GString *str, guint idt, const gchar *key, guint64 plugin_flags)
{
	g_autoptr(GString) tmp = g_string_new("");
	for (guint i = 0; i < 64; i++) {
		if ((plugin_flags & ((guint64)1 << i)) == 0)
			continue;
		g_string_append_printf(tmp, "%s|", fwupd_plugin_flag_to_string((guint64)1 << i));
	}
	if (tmp->len == 0) {
		g_string_append(tmp, fwupd_plugin_flag_to_string(0));
	} else {
		g_string_truncate(tmp, tmp->len - 1);
	}
	fwupd_codec_string_append(str, idt, key, tmp->str);
}

static void
fwupd_plugin_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FwupdPlugin *self = FWUPD_PLUGIN(codec);
	FwupdPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_PLUGIN(self));
	g_return_if_fail(builder != NULL);

	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_NAME, priv->name);
	if (priv->flags != FWUPD_PLUGIN_FLAG_NONE) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fwupd_plugin_flag_to_string((guint64)1 << i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
}

static void
fwupd_plugin_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FwupdPlugin *self = FWUPD_PLUGIN(codec);
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_plugin_string_append_flags(str, idt, FWUPD_RESULT_KEY_FLAGS, priv->flags);
}

static void
fwupd_plugin_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdPlugin *self = FWUPD_PLUGIN(object);
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string(value, priv->name);
		break;
	case PROP_FLAGS:
		g_value_set_uint64(value, priv->flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_plugin_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FwupdPlugin *self = FWUPD_PLUGIN(object);
	switch (prop_id) {
	case PROP_NAME:
		fwupd_plugin_set_name(self, g_value_get_string(value));
		break;
	case PROP_FLAGS:
		fwupd_plugin_set_flags(self, g_value_get_uint64(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_plugin_class_init(FwupdPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fwupd_plugin_finalize;
	object_class->get_property = fwupd_plugin_get_property;
	object_class->set_property = fwupd_plugin_set_property;

	/**
	 * FwupdPlugin:name:
	 *
	 * The plugin name.
	 *
	 * Since: 1.5.0
	 */
	pspec =
	    g_param_spec_string("name", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_NAME, pspec);

	/**
	 * FwupdPlugin:flags:
	 *
	 * The plugin flags.
	 *
	 * Since: 1.5.0
	 */
	pspec = g_param_spec_uint64("flags",
				    NULL,
				    NULL,
				    FWUPD_PLUGIN_FLAG_NONE,
				    FWUPD_PLUGIN_FLAG_UNKNOWN,
				    FWUPD_PLUGIN_FLAG_NONE,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLAGS, pspec);
}

static void
fwupd_plugin_init(FwupdPlugin *self)
{
}

static void
fwupd_plugin_finalize(GObject *object)
{
	FwupdPlugin *self = FWUPD_PLUGIN(object);
	FwupdPluginPrivate *priv = GET_PRIVATE(self);
	g_free(priv->name);
	G_OBJECT_CLASS(fwupd_plugin_parent_class)->finalize(object);
}

static void
fwupd_plugin_from_variant_iter(FwupdCodec *codec, GVariantIter *iter)
{
	FwupdPlugin *self = FWUPD_PLUGIN(codec);
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_plugin_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

static void
fwupd_plugin_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fwupd_plugin_add_string;
	iface->add_json = fwupd_plugin_add_json;
	iface->add_variant = fwupd_plugin_add_variant;
	iface->from_variant_iter = fwupd_plugin_from_variant_iter;
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
fwupd_plugin_new(void)
{
	FwupdPlugin *self;
	self = g_object_new(FWUPD_TYPE_PLUGIN, NULL);
	return FWUPD_PLUGIN(self);
}
