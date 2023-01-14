/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-context-hwid.h"
#include "fu-context-private.h"
#include "fu-string.h"

gboolean
fu_context_hwid_dmi_setup(FuContext *self, GError **error)
{
	const gchar *path = g_getenv("FWUPD_SYSFSDMIDIR");
	g_autofree gchar *path_dmi_class = NULL;
	struct {
		const gchar *hwid;
		const gchar *key;
	} map[] = {{FU_HWIDS_KEY_BASEBOARD_MANUFACTURER, "board_vendor"},
		   {FU_HWIDS_KEY_BASEBOARD_PRODUCT, "board_name"},
		   {FU_HWIDS_KEY_BIOS_VENDOR, "bios_vendor"},
		   {FU_HWIDS_KEY_BIOS_VERSION, "bios_version"},
		   {FU_HWIDS_KEY_FAMILY, "product_family"},
		   {FU_HWIDS_KEY_MANUFACTURER, "sys_vendor"},
		   {FU_HWIDS_KEY_PRODUCT_NAME, "product_name"},
		   {FU_HWIDS_KEY_PRODUCT_SKU, "product_sku"},
		   {FU_HWIDS_KEY_ENCLOSURE_KIND, "chassis_type"},
		   {NULL, NULL}};

	/* the values the kernel parsed; these are world-readable */
	if (path != NULL) {
		path_dmi_class = g_build_filename(path, "dmi", "class", NULL);
	} else {
		path_dmi_class = g_strdup("/sys/class/dmi/id");
	}
	if (!g_file_test(path_dmi_class, G_FILE_TEST_IS_DIR)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no %s", path);
		return FALSE;
	}
	for (guint i = 0; map[i].key != NULL; i++) {
		gsize bufsz = 0;
		g_autofree gchar *buf = NULL;
		g_autofree gchar *fn = g_build_filename(path_dmi_class, map[i].key, NULL);
		g_autoptr(GError) error_local = NULL;

		if (!g_file_get_contents(fn, &buf, &bufsz, &error_local)) {
			g_debug("unable to read SMBIOS data from %s: %s", fn, error_local->message);
			continue;
		}

		/* trim trailing newline added by kernel */
		if (buf[bufsz - 1] == '\n')
			buf[bufsz - 1] = 0;
		fu_context_add_hwid_value(self, map[i].hwid, buf);

		if (g_strcmp0(map[i].hwid, FU_HWIDS_KEY_ENCLOSURE_KIND) == 0) {
			guint64 val = 0;
			if (!fu_strtoull(buf,
					 &val,
					 FU_SMBIOS_CHASSIS_KIND_OTHER,
					 FU_SMBIOS_CHASSIS_KIND_LAST,
					 &error_local)) {
				g_warning("ignoring enclosure kind %s", buf);
				continue;
			}
			fu_context_set_chassis_kind(self, val);
		}
	}

	/* success */
	return TRUE;
}
