/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-context-private.h"
#include "fu-fdt-firmware.h"
#include "fu-hwids-private.h"

gboolean
fu_hwids_fdt_setup(FuContext *ctx, FuHwids *self, GError **error)
{
	g_autofree gchar *chassis_type = NULL;
	g_auto(GStrv) compatible = NULL;
	g_autoptr(FuFirmware) fdt_img = NULL;
	g_autoptr(FuFdtImage) fdt_img_fwver = NULL;
	g_autoptr(FuFirmware) fdt = NULL;
	struct {
		const gchar *hwid;
		const gchar *key;
	} map[] = {{FU_HWIDS_KEY_MANUFACTURER, "vendor"},
		   {FU_HWIDS_KEY_FAMILY, "model-name"},
		   {FU_HWIDS_KEY_PRODUCT_NAME, "model"},
		   {NULL, NULL}};

	/* adds compatible GUIDs */
	fdt = fu_context_get_fdt(ctx, error);
	if (fdt == NULL)
		return FALSE;
	fdt_img = fu_firmware_get_image_by_id(fdt, NULL, error);
	if (fdt_img == NULL)
		return FALSE;
	if (!fu_fdt_image_get_attr_strlist(FU_FDT_IMAGE(fdt_img), "compatible", &compatible, error))
		return FALSE;
	for (guint i = 0; compatible[i] != NULL; i++) {
		g_autofree gchar *guid = fwupd_guid_hash_string(compatible[i]);
		g_debug("using %s for DT compatible %s", guid, compatible[i]);
		fu_hwids_add_guid(self, guid);
	}

	/* root node */
	for (guint i = 0; map[i].key != NULL; i++) {
		g_autofree gchar *tmp = NULL;
		fu_fdt_image_get_attr_str(FU_FDT_IMAGE(fdt_img), map[i].key, &tmp, NULL);
		if (tmp == NULL)
			continue;
		fu_hwids_add_value(self, map[i].hwid, tmp);
	}

	/* chassis kind */
	fu_fdt_image_get_attr_str(FU_FDT_IMAGE(fdt_img), "chassis-type", &chassis_type, NULL);
	if (chassis_type != NULL) {
		struct {
			FuSmbiosChassisKind chassis_kind;
			const gchar *dt;
		} chassis_map[] = {{FU_SMBIOS_CHASSIS_KIND_CONVERTIBLE, "convertible"},
				   {FU_SMBIOS_CHASSIS_KIND_EMBEDDED_PC, "embedded"},
				   {FU_SMBIOS_CHASSIS_KIND_HAND_HELD, "handset"},
				   {FU_SMBIOS_CHASSIS_KIND_LAPTOP, "laptop"},
				   {FU_SMBIOS_CHASSIS_KIND_TABLET, "tablet"},
				   {FU_SMBIOS_CHASSIS_KIND_UNKNOWN, NULL}};
		for (guint i = 0; chassis_map[i].dt != NULL; i++) {
			if (g_strcmp0(chassis_type, chassis_map[i].dt) == 0) {
				fu_context_set_chassis_kind(ctx, chassis_map[i].chassis_kind);
				break;
			}
		}
	}

	/* fallback */
	if (g_strv_length(compatible) > 0)
		fu_hwids_add_value(self, FU_HWIDS_KEY_MANUFACTURER, compatible[0]);
	if (g_strv_length(compatible) > 1)
		fu_hwids_add_value(self, FU_HWIDS_KEY_PRODUCT_NAME, compatible[1]);
	if (g_strv_length(compatible) > 2)
		fu_hwids_add_value(self, FU_HWIDS_KEY_FAMILY, compatible[2]);
	if (fu_context_get_chassis_kind(ctx) == FU_SMBIOS_CHASSIS_KIND_UNKNOWN) {
		if (fu_fdt_image_get_attr_str(FU_FDT_IMAGE(fdt_img), "battery", NULL, NULL))
			fu_context_set_chassis_kind(ctx, FU_SMBIOS_CHASSIS_KIND_PORTABLE);
	}
	fdt_img_fwver =
	    fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(fdt), "ibm,firmware-versions", NULL);
	if (fdt_img_fwver != NULL) {
		g_autofree gchar *version = NULL;
		fu_fdt_image_get_attr_str(FU_FDT_IMAGE(fdt_img), "version", &version, NULL);
		fu_hwids_add_value(self, FU_HWIDS_KEY_BIOS_VERSION, version);
	}

	/* success */
	return TRUE;
}
