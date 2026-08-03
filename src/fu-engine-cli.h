/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cli.h"

#define FU_TYPE_ENGINE_CLI (fu_engine_cli_get_type())
G_DECLARE_FINAL_TYPE(FuEngineCli, fu_engine_cli, FU, ENGINE_CLI, FuCli)
