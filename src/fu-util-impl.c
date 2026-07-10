/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-util-common.h"
#include "fu-util-impl.h"

static gboolean
fu_util_get_plugins(FuUtil *self, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) plugins = NULL;

	plugins = self->impl->get_plugins(self, error);
	if (plugins == NULL)
		return FALSE;
	g_ptr_array_sort(plugins, (GCompareFunc)fu_util_plugin_name_sort_cb);
	if (self->as_json) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_array_to_json(plugins, "Plugins", json_obj, FWUPD_CODEC_FLAG_TRUSTED);
		fu_util_print_json_object(self->console, json_obj);
		return TRUE;
	}

	/* print */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autofree gchar *str = fu_util_plugin_to_string(FWUPD_PLUGIN(plugin), 0);
		fu_console_print_literal(self->console, str);
	}

	/* success */
	return TRUE;
}

void
fu_util_impl_cmd_array_add_all(GPtrArray *cmd_array)
{
	fu_util_cmd_array_add(cmd_array,
			      "get-plugins",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Get all enabled plugins registered with the system"),
			      fu_util_get_plugins);
}

FuUtil *
fu_util_new(void)
{
	FuUtil *self = g_new0(FuUtil, 1);
	self->console = fu_console_new();
	return self;
}

void
fu_util_free(FuUtil *self)
{
	if (self->console != NULL)
		g_object_unref(self->console);
	g_free(self);
}
