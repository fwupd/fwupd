/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuAspeedAst2x00Plugin,
		     fu_aspeed_ast2x00_plugin,
		     FU,
		     ASPEED_AST2X00_PLUGIN,
		     FuPlugin)
