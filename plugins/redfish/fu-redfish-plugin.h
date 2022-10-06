/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuRedfishPlugin, fu_redfish_plugin, FU, REDFISH_PLUGIN, FuPlugin)

void
fu_redfish_plugin_set_credentials(FuPlugin *plugin, const gchar *username, const gchar *password);
