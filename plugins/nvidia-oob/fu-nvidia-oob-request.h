/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <curl/curl.h>

#define FU_TYPE_NVIDIA_OOB_REQUEST (fu_nvidia_oob_request_get_type())
G_DECLARE_FINAL_TYPE(FuNvidiaOobRequest, fu_nvidia_oob_request, FU, NVIDIA_OOB_REQUEST, GObject)

FuNvidiaOobRequest *
fu_nvidia_oob_request_new(const gchar *base_uri, gboolean cacheck);
void
fu_nvidia_oob_request_set_session_key(FuNvidiaOobRequest *self, const gchar *session_key);
void
fu_nvidia_oob_request_set_credentials(FuNvidiaOobRequest *self,
				      const gchar *username,
				      const gchar *password);
gboolean
fu_nvidia_oob_request_perform(FuNvidiaOobRequest *self, const gchar *path, GError **error);
gboolean
fu_nvidia_oob_request_post(FuNvidiaOobRequest *self,
			   const gchar *path,
			   FwupdJsonObject *json_obj,
			   GError **error);
gboolean
fu_nvidia_oob_request_post_mime(FuNvidiaOobRequest *self,
				const gchar *path,
				curl_mime *mime,
				gchar **location,
				GError **error);
curl_mime *
fu_nvidia_oob_request_mime_new(FuNvidiaOobRequest *self);
FwupdJsonObject *
fu_nvidia_oob_request_get_json_object(FuNvidiaOobRequest *self, GError **error);
glong
fu_nvidia_oob_request_get_status_code(FuNvidiaOobRequest *self);
