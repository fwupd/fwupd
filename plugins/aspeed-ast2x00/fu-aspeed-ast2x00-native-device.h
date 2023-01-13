/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-aspeed-ast2x00-device.h"

#define FU_TYPE_ASPEED_AST2X00_NATIVE_DEVICE (fu_aspeed_ast2x00_native_device_get_type())
G_DECLARE_FINAL_TYPE(FuAspeedAst2x00NativeDevice,
		     fu_aspeed_ast2x00_native_device,
		     FU,
		     ASPEED_AST2X00_NATIVE_DEVICE,
		     FuAspeedAst2x00Device)
