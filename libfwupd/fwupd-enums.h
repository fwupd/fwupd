/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-build.h"
#include "fwupd-enums-struct.h"

G_BEGIN_DECLS

/**
 * FWUPD_BATTERY_LEVEL_INVALID:
 *
 * This value signifies the battery level is either unset, or the value cannot
 * be discovered.
 */
#define FWUPD_BATTERY_LEVEL_INVALID 101

const gchar *
fwupd_status_to_string(FwupdStatus status);
FwupdStatus
fwupd_status_from_string(const gchar *status);
const gchar *
fwupd_device_flag_to_string(FwupdDeviceFlags device_flag);
FwupdDeviceFlags
fwupd_device_flag_from_string(const gchar *device_flag);
const gchar *
fwupd_device_problem_to_string(FwupdDeviceProblem device_problem);
FwupdDeviceProblem
fwupd_device_problem_from_string(const gchar *device_problem);
const gchar *
fwupd_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
FwupdPluginFlags
fwupd_plugin_flag_from_string(const gchar *plugin_flag);
const gchar *
fwupd_release_flag_to_string(FwupdReleaseFlags release_flag);
FwupdReleaseFlags
fwupd_release_flag_from_string(const gchar *release_flag);
const gchar *
fwupd_release_urgency_to_string(FwupdReleaseUrgency release_urgency);
FwupdReleaseUrgency
fwupd_release_urgency_from_string(const gchar *release_urgency);
const gchar *
fwupd_update_state_to_string(FwupdUpdateState update_state);
FwupdUpdateState
fwupd_update_state_from_string(const gchar *update_state);
const gchar *
fwupd_feature_flag_to_string(FwupdFeatureFlags feature_flag);
FwupdFeatureFlags
fwupd_feature_flag_from_string(const gchar *feature_flag);
FwupdVersionFormat
fwupd_version_format_from_string(const gchar *str);
const gchar *
fwupd_version_format_to_string(FwupdVersionFormat kind);
FwupdInstallFlags
fwupd_install_flags_from_string(const gchar *str);
const gchar *
fwupd_install_flags_to_string(FwupdInstallFlags install_flags);

G_END_DECLS
