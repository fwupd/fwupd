/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-redfish-request.h"

#define FU_REDFISH_TYPE_BACKEND (fu_redfish_backend_get_type())

G_DECLARE_FINAL_TYPE(FuRedfishBackend, fu_redfish_backend, FU, REDFISH_BACKEND, FuBackend)

FuRedfishBackend *
fu_redfish_backend_new(FuContext *ctx);
const gchar *
fu_redfish_backend_get_vendor(FuRedfishBackend *self);
const gchar *
fu_redfish_backend_get_version(FuRedfishBackend *self);
const gchar *
fu_redfish_backend_get_uuid(FuRedfishBackend *self);
void
fu_redfish_backend_set_hostname(FuRedfishBackend *self, const gchar *hostname);
void
fu_redfish_backend_set_username(FuRedfishBackend *self, const gchar *username);
const gchar *
fu_redfish_backend_get_username(FuRedfishBackend *self);
void
fu_redfish_backend_set_password(FuRedfishBackend *self, const gchar *password);
void
fu_redfish_backend_set_port(FuRedfishBackend *self, guint port);
void
fu_redfish_backend_set_https(FuRedfishBackend *self, gboolean use_https);
void
fu_redfish_backend_set_cacheck(FuRedfishBackend *self, gboolean cacheck);
void
fu_redfish_backend_set_wildcard_targets(FuRedfishBackend *self, gboolean wildcard_targets);
const gchar *
fu_redfish_backend_get_push_uri_path(FuRedfishBackend *self);
FuRedfishRequest *
fu_redfish_backend_request_new(FuRedfishBackend *self);
