/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ASPEED_AST2X00_DEVICE (fu_aspeed_ast2x00_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuAspeedAst2x00Device,
			 fu_aspeed_ast2x00_device,
			 FU,
			 ASPEED_AST2X00_DEVICE,
			 FuUdevDevice)

struct _FuAspeedAst2x00DeviceClass {
	FuUdevDeviceClass parent_class;
};

typedef enum {
	FU_ASPEED_AST2400 = 0x4,
	FU_ASPEED_AST2500 = 0x5,
	FU_ASPEED_AST2600 = 0x6,
} FuAspeedAst2x00Revision;

FuAspeedAst2x00Revision
fu_aspeed_ast2x00_device_get_revision(FuAspeedAst2x00Device *self);
