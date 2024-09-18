/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-daemon.h"

#define FU_TYPE_DBUS_DAEMON (fu_dbus_daemon_get_type())
G_DECLARE_FINAL_TYPE(FuDbusDaemon, fu_dbus_daemon, FU, DBUS_DAEMON, FuDaemon)
