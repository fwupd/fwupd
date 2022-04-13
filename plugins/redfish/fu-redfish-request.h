/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <curl/curl.h>

#define FU_TYPE_REDFISH_REQUEST (fu_redfish_request_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishRequest, fu_redfish_request, FU, REDFISH_REQUEST, GObject)

typedef enum {
	FU_REDFISH_REQUEST_PERFORM_FLAG_NONE = 0,
	FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON = 1 << 0,
	FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE = 1 << 1,
} FuRedfishRequestPerformFlags;

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
#ifdef HAVE_LIBCURL_7_62_0
CURLU *
fu_redfish_request_get_uri(FuRedfishRequest *self);
#else
void
fu_redfish_request_set_uri_base(FuRedfishRequest *self, const gchar *uri_base);
#endif
glong
fu_redfish_request_get_status_code(FuRedfishRequest *self);
void
fu_redfish_request_set_cache(FuRedfishRequest *self, GHashTable *cache);
