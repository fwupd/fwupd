/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuUefiRecoveryPlugin,
		     fu_uefi_recovery_plugin,
		     FU,
		     UEFI_RECOVERY_PLUGIN,
		     FuPlugin)
