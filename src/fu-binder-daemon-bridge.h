/*
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-binder-daemon.h"

G_BEGIN_DECLS

void
fu_binder_bridge_emit_device_added(FuBinderDaemon *self, FwupdDevice *device);
void
fu_binder_bridge_emit_device_removed(FuBinderDaemon *self, FwupdDevice *device);
void
fu_binder_bridge_emit_device_changed(FuBinderDaemon *self, FwupdDevice *device);
void
fu_binder_bridge_emit_device_request(FuBinderDaemon *self, FwupdRequest *request);
void
fu_binder_bridge_emit_changed(FuBinderDaemon *self);
void
fu_binder_bridge_shutdown(FuBinderDaemon *self);

G_END_DECLS
