/*
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <android/binder_ibinder.h>

#include <fwupd.h>

G_BEGIN_DECLS

AIBinder *
fu_binder_cli_bridge_get_service_handle(GError **error);
gboolean
fu_binder_cli_bridge_connect_client(AIBinder *binder, FwupdClient *client, GError **error);
gboolean
fu_binder_cli_bridge_setup_listener(AIBinder *binder_handle, FwupdClient *client, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_devices(AIBinder *binder, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_upgrades(AIBinder *binder, const char *device_id, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_remotes(AIBinder *binder, GError **error);
gboolean
fu_binder_cli_bridge_install(AIBinder *binder_handle,
			     const char *id,
			     int fd,
			     FwupdInstallFlags install_flags,
			     GError **error);

G_END_DECLS
