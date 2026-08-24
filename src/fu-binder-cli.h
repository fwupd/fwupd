/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cli.h"

G_BEGIN_DECLS

#define FU_TYPE_BINDER_CLI (fu_binder_cli_get_type())
G_DECLARE_FINAL_TYPE(FuBinderCli, fu_binder_cli, FU, BINDER_CLI, FuCli)

G_END_DECLS
