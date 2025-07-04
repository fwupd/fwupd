/*
 * Copyright 2023 Dell Technologies
 * Copyright 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mediatek-scaler-common.h"

gchar *
fu_mediatek_scaler_version_to_string(guint32 val)
{
	return g_strdup_printf("%u.%u.%u", val & 0xff, (val >> 24) & 0xff, (val >> 16) & 0xff);
}
