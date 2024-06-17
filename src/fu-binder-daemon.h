/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-daemon.h"

#define FU_TYPE_BINDER_DAEMON (fu_binder_daemon_get_type())
G_DECLARE_FINAL_TYPE(FuBinderDaemon, fu_binder_daemon, FU, BINDER_DAEMON, FuDaemon)
