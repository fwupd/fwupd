/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cli.h"

#define FU_TYPE_TOOL_CLI (fu_tool_cli_get_type())
G_DECLARE_FINAL_TYPE(FuToolCli, fu_tool_cli, FU, TOOL_CLI, FuCli)
