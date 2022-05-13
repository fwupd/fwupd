/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_DAEMON (fu_daemon_get_type())
G_DECLARE_FINAL_TYPE(FuDaemon, fu_daemon, FU, DAEMON, GObject)

typedef enum {
	FU_DAEMON_MACHINE_KIND_UNKNOWN,
	FU_DAEMON_MACHINE_KIND_PHYSICAL,
	FU_DAEMON_MACHINE_KIND_VIRTUAL,
	FU_DAEMON_MACHINE_KIND_CONTAINER,
} FuDaemonMachineKind;

FuDaemon *
fu_daemon_new(void);
gboolean
fu_daemon_setup(FuDaemon *self, const gchar *socket_address, GError **error);
void
fu_daemon_start(FuDaemon *self);
void
fu_daemon_stop(FuDaemon *self);
void
fu_daemon_set_machine_kind(FuDaemon *self, FuDaemonMachineKind machine_kind);
