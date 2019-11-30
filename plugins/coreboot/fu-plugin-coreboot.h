/*
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-device.h"

gchar		*fu_plugin_coreboot_version_string_to_triplet	(const gchar	*coreboot_version,
								 GError		**error);
gchar		*fu_plugin_coreboot_get_name_for_type		(FuPlugin	*plugin,
								const gchar	*vboot_partition);
const gchar	*fu_plugin_coreboot_get_version_string		(FuPlugin	*plugin);
