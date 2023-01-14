/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-context-hwid.h"
#include "fu-context-private.h"
#include "fu-smbios-private.h"
#include "fu-string.h"

typedef gchar *(*FuContextHwidConvertFunc)(FuSmbios *smbios,
					   guint8 type,
					   guint8 offset,
					   GError **error);

static gchar *
fu_context_hwid_smbios_convert_string_table_cb(FuSmbios *smbios,
					       guint8 type,
					       guint8 offset,
					       GError **error)
{
	const gchar *tmp = fu_smbios_get_string(smbios, type, offset, error);
	if (tmp == NULL)
		return NULL;
	/* ComputerHardwareIds.exe seems to strip spaces */
	return fu_strstrip(tmp);
}

static gchar *
fu_context_hwid_smbios_convert_padded_integer_cb(FuSmbios *smbios,
						 guint8 type,
						 guint8 offset,
						 GError **error)
{
	guint tmp = fu_smbios_get_integer(smbios, type, offset, error);
	if (tmp == G_MAXUINT)
		return NULL;
	return g_strdup_printf("%02x", tmp);
}

static gchar *
fu_context_hwid_smbios_convert_integer_cb(FuSmbios *smbios,
					  guint8 type,
					  guint8 offset,
					  GError **error)
{
	guint tmp = fu_smbios_get_integer(smbios, type, offset, error);
	if (tmp == G_MAXUINT)
		return NULL;
	return g_strdup_printf("%x", tmp);
}

gboolean
fu_context_hwid_smbios_setup(FuContext *self, GError **error)
{
	FuSmbios *smbios = fu_context_get_smbios(self);
	struct {
		const gchar *key;
		guint8 type;
		guint8 offset;
		FuContextHwidConvertFunc func;
	} map[] = {{FU_HWIDS_KEY_MANUFACTURER,
		    FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
		    0x04,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {FU_HWIDS_KEY_ENCLOSURE_KIND,
		    FU_SMBIOS_STRUCTURE_TYPE_CHASSIS,
		    0x05,
		    fu_context_hwid_smbios_convert_integer_cb},
		   {FU_HWIDS_KEY_FAMILY,
		    FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
		    0x1a,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {FU_HWIDS_KEY_PRODUCT_NAME,
		    FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
		    0x05,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {FU_HWIDS_KEY_PRODUCT_SKU,
		    FU_SMBIOS_STRUCTURE_TYPE_SYSTEM,
		    0x19,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {FU_HWIDS_KEY_BIOS_VENDOR,
		    FU_SMBIOS_STRUCTURE_TYPE_BIOS,
		    0x04,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {FU_HWIDS_KEY_BIOS_VERSION,
		    FU_SMBIOS_STRUCTURE_TYPE_BIOS,
		    0x05,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {FU_HWIDS_KEY_BIOS_MAJOR_RELEASE,
		    FU_SMBIOS_STRUCTURE_TYPE_BIOS,
		    0x14,
		    fu_context_hwid_smbios_convert_padded_integer_cb},
		   {FU_HWIDS_KEY_BIOS_MINOR_RELEASE,
		    FU_SMBIOS_STRUCTURE_TYPE_BIOS,
		    0x15,
		    fu_context_hwid_smbios_convert_padded_integer_cb},
		   {FU_HWIDS_KEY_FIRMWARE_MAJOR_RELEASE,
		    FU_SMBIOS_STRUCTURE_TYPE_BIOS,
		    0x16,
		    fu_context_hwid_smbios_convert_padded_integer_cb},
		   {FU_HWIDS_KEY_FIRMWARE_MINOR_RELEASE,
		    FU_SMBIOS_STRUCTURE_TYPE_BIOS,
		    0x17,
		    fu_context_hwid_smbios_convert_padded_integer_cb},
		   {FU_HWIDS_KEY_BASEBOARD_MANUFACTURER,
		    FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD,
		    0x04,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {FU_HWIDS_KEY_BASEBOARD_PRODUCT,
		    FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD,
		    0x05,
		    fu_context_hwid_smbios_convert_string_table_cb},
		   {NULL, 0x00, 0x00, NULL}};

	if (!fu_smbios_setup(smbios, error))
		return FALSE;

	/* get all DMI data from SMBIOS */
	fu_context_set_chassis_kind(
	    self,
	    fu_smbios_get_integer(smbios, FU_SMBIOS_STRUCTURE_TYPE_CHASSIS, 0x05, NULL));
	for (guint i = 0; map[i].key != NULL; i++) {
		const gchar *contents_hdr = NULL;
		g_autofree gchar *contents = NULL;
		g_autoptr(GError) error_local = NULL;

		contents = map[i].func(smbios, map[i].type, map[i].offset, &error_local);
		if (contents == NULL) {
			g_debug("ignoring %s: %s", map[i].key, error_local->message);
			continue;
		}
		g_debug("smbios property %s=%s", map[i].key, contents);

		/* weirdly, remove leading zeros */
		contents_hdr = contents;
		while (contents_hdr[0] == '0' &&
		       map[i].func != fu_context_hwid_smbios_convert_padded_integer_cb)
			contents_hdr++;
		fu_context_add_hwid_value(self, map[i].key, contents_hdr);
	}

	/* success */
	return TRUE;
}
