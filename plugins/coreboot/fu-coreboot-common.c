/*
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "fu-plugin-coreboot.h"

/**
 * FU_QUIRKS_COREBOOT_VERSION:
 * @key: The SMBIOS manufacturer name
 * @value: One of the following: "lenovo-cbet-prefix"
 *
 * "lenovo-cbet-prefix" quirk:
 * The thinkpad_acpi kernel module requires a specific pattern
 * in the DMI version string. To satisfy those requirements
 * coreboot adds the CBETxxxx prefix to the DMI version string
 * on all Lenovo devices. The prefix isn't present in the
 * version string found in coreboot tables, or on other
 * coreboot enabled devices.
 *
 * Since: 1.3.5
 */
#define	FU_QUIRKS_COREBOOT_VERSION	"CorebootVersionQuirks"
#define	FU_QUIRK_CBET_PREFIX		"lenovo-cbet-prefix"

/* Tries to convert the coreboot version string to a triplet string.
 * Returns NULL on error. */
gchar *
fu_plugin_coreboot_version_string_to_triplet (const gchar *coreboot_version,
					      GError **error)
{
	guint cb_major = 0;
	guint cb_minor = 0;
	guint cb_build = 0;
	gint rc;

	rc = sscanf (coreboot_version, "%u.%u-%u", &cb_major, &cb_minor, &cb_build);

	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to parse firmware version");
		return NULL;
	}

	/* Sanity check */
	if (cb_major == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Invalid firmware version");
		return NULL;
	}

	return g_strdup_printf ("%u.%u.%u", cb_major, cb_minor, cb_build);
}

/* convert firmware type to user friendly string representation */
gchar *
fu_plugin_coreboot_get_name_for_type (FuPlugin *plugin,
				      const gchar *vboot_partition)
{
	GString *display_name;

	if (vboot_partition != NULL) {
		display_name = g_string_new (vboot_partition);
		g_string_prepend (display_name, ", VBOOT partition ");
	} else {
		display_name = g_string_new ("");
	}

	g_string_prepend (display_name, "coreboot System Firmware");
	return g_string_free (display_name, FALSE);
}

/* Returns the version string with possible quirks applied */
const gchar *
fu_plugin_coreboot_get_version_string (FuPlugin *plugin)
{
	const gchar *version;
	const gchar *manufacturer;
	const gchar *quirk = NULL;

	version = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VERSION);
	if (version == NULL)
		return NULL;

	manufacturer = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (manufacturer != NULL) {
		g_autofree gchar *group = NULL;

		/* any quirks match */
		group = g_strdup_printf ("SmbiosManufacturer=%s", manufacturer);
		quirk = fu_plugin_lookup_quirk_by_id (plugin, group,
						      FU_QUIRKS_COREBOOT_VERSION);
	}

	if (quirk == NULL)
		return version;

	if (g_strcmp0(quirk, FU_QUIRK_CBET_PREFIX) == 0) {
		if (strlen (version) > 9 && g_str_has_prefix (version, "CBET"))
			version += 9;
		return version;
	}

	return version;
}
