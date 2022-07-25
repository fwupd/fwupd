/*
 * Copyright (C) 2021 Dell Inc.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_INTEL_THUNDERBOLT_NVM (fu_intel_thunderbolt_nvm_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIntelThunderboltNvm,
			 fu_intel_thunderbolt_nvm,
			 FU,
			 INTEL_THUNDERBOLT_NVM,
			 FuFirmware)

struct _FuIntelThunderboltNvmClass {
	FuFirmwareClass parent_class;
};

guint16
fu_intel_thunderbolt_nvm_get_vendor_id(FuIntelThunderboltNvm *self);
guint16
fu_intel_thunderbolt_nvm_get_device_id(FuIntelThunderboltNvm *self);
guint16
fu_intel_thunderbolt_nvm_get_model_id(FuIntelThunderboltNvm *self);
gboolean
fu_intel_thunderbolt_nvm_is_host(FuIntelThunderboltNvm *self);
gboolean
fu_intel_thunderbolt_nvm_is_native(FuIntelThunderboltNvm *self);
gboolean
fu_intel_thunderbolt_nvm_has_pd(FuIntelThunderboltNvm *self);
guint8
fu_intel_thunderbolt_nvm_get_flash_size(FuIntelThunderboltNvm *self);

FuFirmware *
fu_intel_thunderbolt_nvm_new(void);
