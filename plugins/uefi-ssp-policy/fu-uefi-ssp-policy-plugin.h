/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuUefiSspPolicyPlugin,
		     fu_uefi_ssp_policy_plugin,
		     FU,
		     UEFI_SSP_POLICY_PLUGIN,
		     FuPlugin)
