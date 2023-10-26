/*
 * Copyright (C) 2023 Dell Technologies
 * Copyright (C) 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mediatek-scaler-common.h"

gchar *
mediatek_scaler_device_version_to_string(guint32 val)
{
	return g_strdup_printf("%x.%x.%x", val & 0xff, (val >> 24) & 0xff, (val >> 16) & 0xff);
}
