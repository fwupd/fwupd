/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <libfwupd/fwupd-bios-setting.h>

#define FU_TYPE_FIRMWARE_ATTRS (fu_bios_settings_get_type())

G_DECLARE_FINAL_TYPE(FuBiosSettings, fu_bios_settings, FU, BIOS_SETTINGS, GObject)

FuBiosSettings *
fu_bios_settings_new(void);
gboolean
fu_bios_settings_get_pending_reboot(FuBiosSettings *self, gboolean *result, GError **error);
FwupdBiosSetting *
fu_bios_settings_get_attr(FuBiosSettings *self, const gchar *val);
