/*
 * Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-client.h"

#define FU_TYPE_CLIENT_LIST (fu_client_list_get_type())
G_DECLARE_FINAL_TYPE(FuClientList, fu_client_list, FU, CLIENT_LIST, GObject)

FuClientList *
fu_client_list_new(GDBusConnection *connection);
GPtrArray *
fu_client_list_get_all(FuClientList *self);
FuClient *
fu_client_list_register(FuClientList *self, const gchar *sender);
FuClient *
fu_client_list_get_by_sender(FuClientList *self, const gchar *sender);
