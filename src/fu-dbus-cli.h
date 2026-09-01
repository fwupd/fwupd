/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cli.h"

G_BEGIN_DECLS

#define FU_TYPE_DBUS_CLI (fu_dbus_cli_get_type())
G_DECLARE_FINAL_TYPE(FuDbusCli, fu_dbus_cli, FU, DBUS_CLI, FuCli)

G_END_DECLS
