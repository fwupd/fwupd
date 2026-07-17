/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <android/binder_ibinder.h>
#include <glib-object.h>

#include "fu-daemon.h"

G_BEGIN_DECLS

#define FU_TYPE_BINDER_DAEMON (fu_binder_daemon_get_type())

G_DECLARE_FINAL_TYPE(FuBinderDaemon, fu_binder_daemon, FU, BINDER_DAEMON, FuDaemon)

GVariant *
fu_binder_daemon_get_devices_as_variant(FuBinderDaemon *self, GError **error);
void
fu_binder_daemon_register_service_handle(FuBinderDaemon *self, AIBinder *binder);

G_END_DECLS
