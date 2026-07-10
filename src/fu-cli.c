/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCli"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-cli.h"
#include "fu-console.h"
#include "fu-util-common.h"

typedef struct {
	GPtrArray *cmd_array;
	FuConsole *console;
	gboolean as_json;
} FuCliPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCli, fu_cli, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_cli_get_instance_private(o))

gboolean
fu_cli_get_as_json(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return priv->as_json;
}

FuConsole *
fu_cli_get_console(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return priv->console;
}

static gboolean
fu_cli_cmd_get_plugins(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FuCliClass *klass = FU_CLI_GET_CLASS(self);
	g_autoptr(GPtrArray) plugins = NULL;

	if (klass->get_plugins == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no impl");
		return FALSE;
	}
	plugins = klass->get_plugins(self, error);
	if (plugins == NULL)
		return FALSE;
	g_ptr_array_sort(plugins, (GCompareFunc)fu_util_plugin_name_sort_cb);
	if (priv->as_json) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_array_to_json(plugins, "Plugins", json_obj, FWUPD_CODEC_FLAG_TRUSTED);
		fu_util_print_json_object(priv->console, json_obj);
		return TRUE;
	}

	/* print */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autofree gchar *str = fu_util_plugin_to_string(FWUPD_PLUGIN(plugin), 0);
		fu_console_print_literal(priv->console, str);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_group_pre_parse_cb(GOptionContext *context,
			  GOptionGroup *group,
			  gpointer data,
			  GError **error)
{
	FuCli *self = FU_CLI(data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	const GOptionEntry entries[] = {
	    {"json",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->as_json,
	     /* TRANSLATORS: command line option */
	     N_("Output in JSON format (disables all interactive prompts)"),
	     NULL},
	    {NULL}};
	g_option_group_add_entries(group, entries);
	return TRUE;
}

GOptionGroup *
fu_cli_get_option_group(FuCli *self)
{
	GOptionGroup *group = g_option_group_new("general",
						 /* TRANSLATORS: for the --verbose arg */
						 _("Debugging Options"),
						 /* TRANSLATORS: for the --verbose arg */
						 _("Show debugging options"),
						 self,
						 NULL);
	g_option_group_set_parse_hooks(group, fu_cli_group_pre_parse_cb, NULL);
	return group;
}

void
fu_cli_cmd_array_add_common(FuCli *self)
{
	fu_cli_cmd_array_add(self,
			     "get-plugins",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Get all enabled plugins registered with the system"),
			     fu_cli_cmd_get_plugins);
}

typedef struct {
	gchar *name;
	gchar *arguments;
	gchar *description;
	FuUtilCmdFlags flags;
	FuCliCmdFunc callback;
} FuCliCmd;

static void
fu_cli_cmd_free(FuCliCmd *item)
{
	g_free(item->name);
	g_free(item->arguments);
	g_free(item->description);
	g_free(item);
}

static gint
fu_cli_cmd_sort_cb(FuCliCmd **item1, FuCliCmd **item2)
{
	return g_strcmp0((*item1)->name, (*item2)->name);
}

void
fu_cli_cmd_array_sort(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_ptr_array_sort(priv->cmd_array, (GCompareFunc)fu_cli_cmd_sort_cb);
}

void
fu_cli_cmd_array_add(FuCli *self,
		     const gchar *name,
		     const gchar *arguments,
		     const gchar *description,
		     FuCliCmdFunc callback)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) names = NULL;

	g_return_if_fail(name != NULL);
	g_return_if_fail(description != NULL);
	g_return_if_fail(callback != NULL);

	/* add each one */
	names = g_strsplit(name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuCliCmd *item = g_new0(FuCliCmd, 1);
		item->name = g_strdup(names[i]);
		if (i == 0) {
			item->description = g_strdup(description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf(_("Alias to %s"), names[0]);
			item->flags |= FU_UTIL_CMD_FLAG_IS_ALIAS;
		}
		item->arguments = g_strdup(arguments);
		item->callback = callback;
		g_ptr_array_add(priv->cmd_array, item);
	}
}

gboolean
fu_cli_cmd_array_run(FuCli *self, const gchar *command, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) values_copy = g_new0(gchar *, g_strv_length(values) + 1);

	/* clear out bash completion sentinel */
	for (guint i = 0; values[i] != NULL; i++) {
		if (g_strcmp0(values[i], "{") == 0) /* nocheck:depth */
			break;
		values_copy[i] = g_strdup(values[i]);
	}

	/* return all possible actions */
	if (g_strcmp0(command, "get-actions") == 0) {
		for (guint i = 0; i < priv->cmd_array->len; i++) {
			FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
			if (item->flags & FU_UTIL_CMD_FLAG_IS_ALIAS)
				continue;
			g_print("%s\n", item->name); /* nocheck:print */
		}
		return TRUE;
	}

	/* find command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
		if (g_strcmp0(item->name, command) == 0)
			return item->callback(self, values_copy, error);
	}

	/* not found */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    /* TRANSLATORS: error message */
			    _("Command not found"));
	return FALSE;
}

gchar *
fu_cli_cmd_array_to_string(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	gsize len;
	const gsize max_len = 35;
	GString *string = g_string_new(NULL);

	/* print each command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
		g_string_append(string, "  ");
		g_string_append(string, item->name);
		len = fu_strwidth(item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append(string, " ");
			g_string_append(string, item->arguments);
			len += fu_strwidth(item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		} else {
			g_string_append_c(string, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size(string, string->len - 1);

	return g_string_free(string, FALSE);
}

static void
fu_cli_init(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->cmd_array = g_ptr_array_new_with_free_func((GDestroyNotify)fu_cli_cmd_free);
	priv->console = fu_console_new();
}

static void
fu_cli_finalize(GObject *obj)
{
	FuCli *self = FU_CLI(obj);
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_object_unref(priv->console);
	g_ptr_array_unref(priv->cmd_array);
	G_OBJECT_CLASS(fu_cli_parent_class)->finalize(obj);
}

static void
fu_cli_class_init(FuCliClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cli_finalize;
}

FuCli *
fu_cli_new(void)
{
	return g_object_new(FU_TYPE_CLI, NULL);
}
