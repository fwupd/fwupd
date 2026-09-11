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

G_END_DECLS
