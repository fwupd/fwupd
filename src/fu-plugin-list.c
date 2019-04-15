/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuPluginList"

#include "config.h"

#include <glib-object.h>

#include "fu-plugin-list.h"
#include "fu-plugin-private.h"

#include "fwupd-error.h"

/**
 * SECTION:fu-plugin-list
 * @short_description: a list of plugins
 *
 * This list of plugins provides a way to get the specific plugin quickly using
 * a hash table and also any plugin-list specific functionality such as
 * sorting by dependency order.
 *
 * See also: #FuPlugin
 */

static void fu_plugin_list_finalize	 (GObject *obj);

struct _FuPluginList
{
	GObject			 parent_instance;
	GPtrArray		*plugins;		/* of FuPlugin */
	GHashTable		*plugins_hash;		/* of name : FuPlugin */
};

G_DEFINE_TYPE (FuPluginList, fu_plugin_list, G_TYPE_OBJECT)

/**
 * fu_plugin_list_get_all:
 * @self: A #FuPluginList
 *
 * Gets all the plugins that have been added.
 *
 * Returns: (transfer none) (element-type FuPlugin): the plugins
 *
 * Since: 1.0.2
 **/
GPtrArray *
fu_plugin_list_get_all (FuPluginList *self)
{
	g_return_val_if_fail (FU_IS_PLUGIN_LIST (self), NULL);
	return self->plugins;
}

/**
 * fu_plugin_list_add:
 * @self: A #FuPluginList
 * @plugin: A #FuPlugin
 *
 * Adds a plugin to the list. The plugin name is used for a hash key and must
 * be set before calling this function.
 *
 * Since: 1.0.2
 **/
void
fu_plugin_list_add (FuPluginList *self, FuPlugin *plugin)
{
	g_return_if_fail (FU_IS_PLUGIN_LIST (self));
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (fu_plugin_get_name (plugin) != NULL);
	g_ptr_array_add (self->plugins, g_object_ref (plugin));
	g_hash_table_insert (self->plugins_hash,
			     g_strdup (fu_plugin_get_name (plugin)),
			     g_object_ref (plugin));
}

/**
 * fu_plugin_list_find_by_name:
 * @self: A #FuPluginList
 * @name: A #FuPlugin name, e.g. "dfu"
 * @error: A #GError, or %NULL
 *
 * Finds a specific plugin using the plugin name.
 *
 * Returns: (transfer none): a plugin, or %NULL
 *
 * Since: 1.0.2
 **/
FuPlugin *
fu_plugin_list_find_by_name (FuPluginList *self, const gchar *name, GError **error)
{
	g_return_val_if_fail (FU_IS_PLUGIN_LIST (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	for (guint i = 0; i < self->plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (self->plugins, i);
		if (g_strcmp0 (fu_plugin_get_name (plugin), name) == 0)
			return plugin;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "no plugin %s found", name);
	return NULL;
}

static gint
fu_plugin_list_sort_cb (gconstpointer a, gconstpointer b)
{
	FuPlugin **pa = (FuPlugin **) a;
	FuPlugin **pb = (FuPlugin **) b;
	return fu_plugin_order_compare (*pa, *pb);
}

/**
 * fu_plugin_list_depsolve:
 * @self: A #FuPluginList
 * @error: A #GError, or %NULL
 *
 * Depsolves the list of plugins into the correct order. Some plugin methods
 * are called on all plugins and for some situations the order they are called
 * may be important. Use fu_plugin_add_rule() to affect the depsolved order
 * if required.
 *
 * Returns: %TRUE for success, or %FALSE if the set could not be depsolved
 *
 * Since: 1.0.2
 **/
gboolean
fu_plugin_list_depsolve (FuPluginList *self, GError **error)
{
	FuPlugin *dep;
	GPtrArray *deps;
	gboolean changes;
	guint dep_loop_check = 0;

	g_return_val_if_fail (FU_IS_PLUGIN_LIST (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* order by deps */
	do {
		changes = FALSE;
		for (guint i = 0; i < self->plugins->len; i++) {
			FuPlugin *plugin = g_ptr_array_index (self->plugins, i);
			deps = fu_plugin_get_rules (plugin, FU_PLUGIN_RULE_RUN_AFTER);
			for (guint j = 0; j < deps->len && !changes; j++) {
				const gchar *plugin_name = g_ptr_array_index (deps, j);
				dep = fu_plugin_list_find_by_name (self, plugin_name, NULL);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s' "
						 "requested by '%s'",
						 plugin_name,
						 fu_plugin_get_name (plugin));
					continue;
				}
				if (!fu_plugin_get_enabled (dep))
					continue;
				if (fu_plugin_get_order (plugin) <= fu_plugin_get_order (dep)) {
					g_debug ("%s [%u] to be ordered after %s [%u] "
						 "so promoting to [%u]",
						 fu_plugin_get_name (plugin),
						 fu_plugin_get_order (plugin),
						 fu_plugin_get_name (dep),
						 fu_plugin_get_order (dep),
						 fu_plugin_get_order (dep) + 1);
					fu_plugin_set_order (plugin, fu_plugin_get_order (dep) + 1);
					changes = TRUE;
				}
			}
		}
		for (guint i = 0; i < self->plugins->len; i++) {
			FuPlugin *plugin = g_ptr_array_index (self->plugins, i);
			deps = fu_plugin_get_rules (plugin, FU_PLUGIN_RULE_RUN_BEFORE);
			for (guint j = 0; j < deps->len && !changes; j++) {
				const gchar *plugin_name = g_ptr_array_index (deps, j);
				dep = fu_plugin_list_find_by_name (self, plugin_name, NULL);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s' "
						 "requested by '%s'",
						 plugin_name,
						 fu_plugin_get_name (plugin));
					continue;
				}
				if (!fu_plugin_get_enabled (dep))
					continue;
				if (fu_plugin_get_order (plugin) >= fu_plugin_get_order (dep)) {
					g_debug ("%s [%u] to be ordered before %s [%u] "
						 "so promoting to [%u]",
						 fu_plugin_get_name (plugin),
						 fu_plugin_get_order (plugin),
						 fu_plugin_get_name (dep),
						 fu_plugin_get_order (dep),
						 fu_plugin_get_order (dep) + 1);
					fu_plugin_set_order (dep, fu_plugin_get_order (plugin) + 1);
					changes = TRUE;
				}
			}
		}

		/* set priority as well */
		for (guint i = 0; i < self->plugins->len; i++) {
			FuPlugin *plugin = g_ptr_array_index (self->plugins, i);
			deps = fu_plugin_get_rules (plugin, FU_PLUGIN_RULE_BETTER_THAN);
			for (guint j = 0; j < deps->len && !changes; j++) {
				const gchar *plugin_name = g_ptr_array_index (deps, j);
				dep = fu_plugin_list_find_by_name (self, plugin_name, NULL);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s' "
						 "referenced by '%s'",
						 plugin_name,
						 fu_plugin_get_name (plugin));
					continue;
				}
				if (!fu_plugin_get_enabled (dep))
					continue;
				if (fu_plugin_get_priority (plugin) <= fu_plugin_get_priority (dep)) {
					g_debug ("%s [%u] better than %s [%u] "
						 "so bumping to [%u]",
						 fu_plugin_get_name (plugin),
						 fu_plugin_get_priority (plugin),
						 fu_plugin_get_name (dep),
						 fu_plugin_get_priority (dep),
						 fu_plugin_get_priority (dep) + 1);
					fu_plugin_set_priority (plugin, fu_plugin_get_priority (dep) + 1);
					changes = TRUE;
				}
			}
		}

		/* check we're not stuck */
		if (dep_loop_check++ > 100) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "got stuck in dep loop");
			return FALSE;
		}
	} while (changes);

	/* check for conflicts */
	for (guint i = 0; i < self->plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (self->plugins, i);
		if (!fu_plugin_get_enabled (plugin))
			continue;
		deps = fu_plugin_get_rules (plugin, FU_PLUGIN_RULE_CONFLICTS);
		for (guint j = 0; j < deps->len && !changes; j++) {
			const gchar *plugin_name = g_ptr_array_index (deps, j);
			dep = fu_plugin_list_find_by_name (self, plugin_name, NULL);
			if (dep == NULL)
				continue;
			if (!fu_plugin_get_enabled (dep))
				continue;
			g_debug ("disabling %s as conflicts with %s",
				 fu_plugin_get_name (dep),
				 fu_plugin_get_name (plugin));
			fu_plugin_set_enabled (dep, FALSE);
		}
	}

	/* sort by order */
	g_ptr_array_sort (self->plugins, fu_plugin_list_sort_cb);
	return TRUE;
}

static void
fu_plugin_list_class_init (FuPluginListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_plugin_list_finalize;
}

static void
fu_plugin_list_init (FuPluginList *self)
{
	self->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->plugins_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
						    g_free, (GDestroyNotify) g_object_unref);
}

static void
fu_plugin_list_finalize (GObject *obj)
{
	FuPluginList *self = FU_PLUGIN_LIST (obj);

	g_ptr_array_unref (self->plugins);
	g_hash_table_unref (self->plugins_hash);

	G_OBJECT_CLASS (fu_plugin_list_parent_class)->finalize (obj);
}

/**
 * fu_plugin_list_new:
 *
 * Creates a new plugin list.
 *
 * Returns: (transfer full): a #FuPluginList
 *
 * Since: 1.0.2
 **/
FuPluginList *
fu_plugin_list_new (void)
{
	FuPluginList *self;
	self = g_object_new (FU_TYPE_PLUGIN_LIST, NULL);
	return FU_PLUGIN_LIST (self);
}
