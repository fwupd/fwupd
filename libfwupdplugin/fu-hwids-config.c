/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-config.h"
#include "fu-context-private.h"
#include "fu-hwids-private.h"
#include "fu-path.h"

gboolean
fu_hwids_config_setup(FuContext *ctx, FuHwids *self, GError **error)
{
	FuConfig *config = fu_context_get_config(ctx);
	g_autoptr(GPtrArray) keys = fu_hwids_get_keys(self);

	/* all keys are optional */
	for (guint i = 0; i < keys->len; i++) {
		const gchar *key = g_ptr_array_index(keys, i);
		g_autofree gchar *value = fu_config_get_value(config, "fwupd", key, NULL);
		if (value != NULL)
			fu_hwids_add_value(self, key, value);
	}

	/* success */
	return TRUE;
}
