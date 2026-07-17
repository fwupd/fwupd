/*
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once
#include <android/binder_ibinder.h>
#include <glib.h>

G_BEGIN_DECLS

AIBinder *
fu_binder_client_get_service_handle_aidl(GError **error);
GVariant *
fu_binder_client_get_devices_aidl(AIBinder *binder, GError **error);
bool
fu_binder_client_setup_listener_aidl(AIBinder *binder_handle);
bool
fu_binder_client_install_aidl(AIBinder *binder_handle,
			      const char *id,
			      int fd,
			      GVariant *options,
			      GError **error);
GVariant *
fu_binder_client_get_upgrades_aidl(AIBinder *binder, const char *device_id, GError **error);
GVariant *
fu_binder_client_get_hwids_aidl(AIBinder *binder, GError **error);

G_END_DECLS
