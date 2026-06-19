/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FWUPD_TYPE_TEST_BIOS_SETTING (fwupd_test_bios_setting_get_type())
G_DECLARE_FINAL_TYPE(FwupdTestBiosSetting,
		     fwupd_test_bios_setting,
		     FWUPD,
		     TEST_BIOS_SETTING,
		     FwupdBiosSetting)

FwupdTestBiosSetting *
fwupd_test_bios_setting_new(void);
const gchar *
fwupd_test_bios_setting_get_value_raw(FwupdTestBiosSetting *self) G_GNUC_NON_NULL(1);
void
fwupd_test_bios_setting_set_value_raw(FwupdTestBiosSetting *self, const gchar *value_raw)
    G_GNUC_NON_NULL(1);
