/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-daemon.h"

G_BEGIN_DECLS

#define FU_TYPE_BINDER_DAEMON (fu_binder_daemon_get_type())

G_DECLARE_FINAL_TYPE(FuBinderDaemon, fu_binder_daemon, FU, BINDER_DAEMON, FuDaemon)

gboolean
fu_binder_daemon_setup_aidl_service(FuBinderDaemon *self, GError **error);

FuEngineRequest *
fu_binder_daemon_create_request(FuBinderDaemon *self);

void
fu_binder_daemon_set_feature_flags(FuBinderDaemon *self, FwupdFeatureFlags feature_flags);

gboolean
fu_binder_daemon_authorize(const gchar *action_id, GError **error);

gboolean
fu_binder_daemon_perform_install_bridge(void *daemon_instance,
					const gchar *device_id,
					int fd,
					guint64 flags,
					GError **error);

gboolean
fu_binder_daemon_activate_bridge(void *daemon_instance, const gchar *device_id, GError **error);

gboolean
fu_binder_daemon_verify_bridge(void *daemon_instance, const gchar *device_id, GError **error);

gboolean
fu_binder_daemon_verify_update_bridge(void *daemon_instance,
				      const gchar *device_id,
				      GError **error);

gboolean
fu_binder_daemon_unlock_bridge(void *daemon_instance, const gchar *device_id, GError **error);

gboolean
fu_binder_daemon_clear_results_bridge(void *daemon_instance,
				      const gchar *device_id,
				      GError **error);

gboolean
fu_binder_daemon_modify_device_bridge(void *daemon_instance,
				      const gchar *device_id,
				      const gchar *key,
				      const gchar *value,
				      GError **error);

gboolean
fu_binder_daemon_modify_remote_bridge(void *daemon_instance,
				      const gchar *remote_id,
				      const gchar *key,
				      const gchar *value,
				      GError **error);

gboolean
fu_binder_daemon_clean_remote_bridge(void *daemon_instance, const gchar *remote_id, GError **error);

gboolean
fu_binder_daemon_modify_config_bridge(void *daemon_instance,
				      const gchar *section,
				      const gchar *key,
				      const gchar *value,
				      GError **error);

gboolean
fu_binder_daemon_reset_config_bridge(void *daemon_instance, const gchar *section, GError **error);

gboolean
fu_binder_daemon_update_metadata_bridge(void *daemon_instance,
					const gchar *remote_id,
					int data_fd,
					int signature_fd,
					GError **error);

gboolean
fu_binder_daemon_set_bios_settings_bridge(void *daemon_instance,
					  GHashTable *settings,
					  GError **error);

G_END_DECLS
