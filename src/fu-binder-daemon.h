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

gboolean
fu_binder_daemon_perform_install_bridge(void *daemon_instance,
					const gchar *device_id,
					int fd,
					guint64 flags,
					GError **error);

G_END_DECLS
