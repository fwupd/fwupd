/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-plugin.h"

#define FU_TYPE_PLUGIN_LIST (fu_plugin_list_get_type ())
G_DECLARE_FINAL_TYPE (FuPluginList, fu_plugin_list, FU, PLUGIN_LIST, GObject)

FuPluginList	*fu_plugin_list_new			(void);
void		 fu_plugin_list_add			(FuPluginList	*self,
							 FuPlugin	*plugin);
GPtrArray	*fu_plugin_list_get_all			(FuPluginList	*self);
FuPlugin	*fu_plugin_list_find_by_name		(FuPluginList	*self,
							 const gchar	*name,
							 GError		**error);
gboolean	 fu_plugin_list_depsolve		(FuPluginList	*self,
							 GError		**error);
