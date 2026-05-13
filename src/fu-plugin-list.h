/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PLUGIN_LIST (fu_plugin_list_get_type())
G_DECLARE_FINAL_TYPE(FuPluginList, fu_plugin_list, FU, PLUGIN_LIST, GObject)

FuPluginList *
fu_plugin_list_new(void);
void
fu_plugin_list_add(FuPluginList *self, FuPlugin *plugin) G_GNUC_NON_NULL(1, 2);
void
fu_plugin_list_remove_all(FuPluginList *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_plugin_list_get_all(FuPluginList *self) G_GNUC_NON_NULL(1);
FuPlugin *
fu_plugin_list_find_by_name(FuPluginList *self, const gchar *name, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_list_depsolve(FuPluginList *self, GError **error) G_GNUC_NON_NULL(1);
