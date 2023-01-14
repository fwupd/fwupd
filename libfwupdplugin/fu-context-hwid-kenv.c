/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-context-hwid.h"
#include "fu-context-private.h"
#include "fu-kenv.h"

gboolean
fu_context_hwid_kenv_setup(FuContext *self, GError **error)
{
#ifdef HAVE_KENV_H
	struct {
		const gchar *hwid;
		const gchar *key;
	} map[] = {{FU_HWIDS_KEY_BASEBOARD_MANUFACTURER, "smbios.planar.maker"},
		   {FU_HWIDS_KEY_BASEBOARD_PRODUCT, "smbios.planar.product"},
		   {FU_HWIDS_KEY_BIOS_VENDOR, "smbios.bios.vendor"},
		   {FU_HWIDS_KEY_BIOS_VERSION, "smbios.bios.version"},
		   {FU_HWIDS_KEY_FAMILY, "smbios.system.family"},
		   {FU_HWIDS_KEY_MANUFACTURER, "smbios.system.maker"},
		   {FU_HWIDS_KEY_PRODUCT_NAME, "smbios.system.product"},
		   {FU_HWIDS_KEY_PRODUCT_SKU, "smbios.system.sku"},
		   {{NULL, NULL}}};
	for (guint i = 0; map[i].key != NULL; i++) {
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *value = fu_kenv_get_string(map[i].key, error_local);
		if (value == NULL) {
			g_debug("ignoring: %s", error_local->message);
			continue;
		}
		fu_context_add_hwid_value(self, map[i].hwid, value);
	}
#endif

	/* success */
	return TRUE;
}
