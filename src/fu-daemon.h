/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-daemon-struct.h"
#include "fu-engine.h"

#define FU_TYPE_DAEMON (fu_daemon_get_type())

G_DECLARE_DERIVABLE_TYPE(FuDaemon, fu_daemon, FU, DAEMON, GObject)

struct _FuDaemonClass {
	GObjectClass parent_class;
	gboolean (*setup)(FuDaemon *self,
			  const gchar *socket_address,
			  FuProgress *progress,
			  GError **error);
	gboolean (*start)(FuDaemon *self, GError **error);
	gboolean (*stop)(FuDaemon *self, GError **error);
};

FuDaemon *
fu_daemon_new(void);

void
fu_daemon_schedule_housekeeping(FuDaemon *self) G_GNUC_NON_NULL(1);
void
fu_daemon_schedule_process_quit(FuDaemon *self) G_GNUC_NON_NULL(1);
gboolean
fu_daemon_setup(FuDaemon *self, const gchar *socket_address, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_daemon_start(FuDaemon *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_daemon_stop(FuDaemon *self, GError **error) G_GNUC_NON_NULL(1);

FuEngine *
fu_daemon_get_engine(FuDaemon *self) G_GNUC_NON_NULL(1);
FuDaemonMachineKind
fu_daemon_get_machine_kind(FuDaemon *self) G_GNUC_NON_NULL(1);
void
fu_daemon_set_machine_kind(FuDaemon *self, FuDaemonMachineKind machine_kind) G_GNUC_NON_NULL(1);
void
fu_daemon_set_update_in_progress(FuDaemon *self, gboolean update_in_progress) G_GNUC_NON_NULL(1);
gboolean
fu_daemon_get_pending_stop(FuDaemon *self) G_GNUC_NON_NULL(1);
