/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-igsc-struct.h"

void
fu_igsc_fwdata_device_info_export(GPtrArray *device_infos, XbBuilderNode *bn) G_GNUC_NON_NULL(1, 2);
gboolean
fu_igsc_fwdata_device_info_parse(GPtrArray *device_infos, FuFirmware *fw, GError **error)
    G_GNUC_NON_NULL(1, 2);

gboolean
fu_igsc_heci_check_status(FuIgscFwuHeciStatus status, GError **error);
