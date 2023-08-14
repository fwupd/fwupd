/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_BIOS_SETTING (fu_bios_setting_get_type())

G_DECLARE_FINAL_TYPE(FuBiosSetting, fu_bios_setting, FU, BIOS_SETTING, FwupdBiosSetting)

FwupdBiosSetting *
fu_bios_setting_new(void);
