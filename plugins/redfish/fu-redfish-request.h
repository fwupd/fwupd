/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <curl/curl.h>

#include "fu-redfish-struct.h"

#define FU_TYPE_REDFISH_REQUEST (fu_redfish_request_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishRequest, fu_redfish_request, FU, REDFISH_REQUEST, GObject)

gboolean
fu_redfish_request_perform(FuRedfishRequest *self,
			   const gchar *path,
			   FuRedfishRequestPerformFlags flags,
			   GError **error);
gboolean
fu_redfish_request_perform_full(FuRedfishRequest *self,
				const gchar *path,
				const gchar *request,
				JsonBuilder *builder,
				FuRedfishRequestPerformFlags flags,
				GError **error);
JsonObject *
fu_redfish_request_get_json_object(FuRedfishRequest *self);
CURL *
fu_redfish_request_get_curl(FuRedfishRequest *self);
void
fu_redfish_request_set_curlsh(FuRedfishRequest *self, CURLSH *curlsh);
CURLU *
fu_redfish_request_get_uri(FuRedfishRequest *self);
glong
fu_redfish_request_get_status_code(FuRedfishRequest *self);
void
fu_redfish_request_set_cache(FuRedfishRequest *self, GHashTable *cache);
