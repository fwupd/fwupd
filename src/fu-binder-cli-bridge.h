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
fu_binder_cli_bridge_get_releases(AIBinder *binder, const char *device_id, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_remotes(AIBinder *binder, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_plugins(AIBinder *binder, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_history(AIBinder *binder, GError **error);
gboolean
fu_binder_cli_bridge_set_feature_flags(AIBinder *binder,
				       FwupdFeatureFlags feature_flags,
				       GError **error);
gboolean
fu_binder_cli_bridge_install(AIBinder *binder_handle,
			     const char *id,
			     int fd,
			     FwupdInstallFlags install_flags,
			     GError **error);
gboolean
fu_binder_cli_bridge_update_metadata(AIBinder *binder,
				     const char *remote_id,
				     int metadata_fd,
				     int signature_fd,
				     GError **error);
gboolean
fu_binder_cli_bridge_activate(AIBinder *binder, const char *device_id, GError **error);
gboolean
fu_binder_cli_bridge_unlock(AIBinder *binder, const char *device_id, GError **error);
gboolean
fu_binder_cli_bridge_verify(AIBinder *binder, const char *device_id, GError **error);
gboolean
fu_binder_cli_bridge_verify_update(AIBinder *binder, const char *device_id, GError **error);
gboolean
fu_binder_cli_bridge_modify_remote(AIBinder *binder,
				   const char *remote_id,
				   const char *key,
				   const char *value,
				   GError **error);
gboolean
fu_binder_cli_bridge_clean_remote(AIBinder *binder, const char *remote_id, GError **error);
gboolean
fu_binder_cli_bridge_modify_device(AIBinder *binder,
				   const char *device_id,
				   const char *key,
				   const char *value,
				   GError **error);
gboolean
fu_binder_cli_bridge_modify_config(AIBinder *binder,
				   const char *section,
				   const char *key,
				   const char *value,
				   GError **error);
gboolean
fu_binder_cli_bridge_reset_config(AIBinder *binder, const char *section, GError **error);
gboolean
fu_binder_cli_bridge_clear_results(AIBinder *binder, const char *device_id, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_details(AIBinder *binder, int fd, GError **error);
GHashTable *
fu_binder_cli_bridge_get_report_metadata(AIBinder *binder, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_bios_settings(AIBinder *binder, GError **error);
gboolean
fu_binder_cli_bridge_modify_bios_settings(AIBinder *binder, GHashTable *settings, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_host_security_attrs(AIBinder *binder, GError **error);
GPtrArray *
fu_binder_cli_bridge_get_host_security_events(AIBinder *binder, guint limit, GError **error);

G_END_DECLS
