/*
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "fu-plugin-coreboot.h"

/* Tries to convert the coreboot version string to a triplet string.
 * Returns NULL on error.
 */
const gchar *
fu_plugin_coreboot_version_string_to_triplet (const gchar *coreboot_version,
					      GError **error)
{
	guint cb_version = 0;
	guint cb_major = 0;
	guint cb_minor = 0;
	guint cb_build = 0;
	gint rc;

	rc = sscanf (coreboot_version, "CBET%u %u.%u-%u", &cb_version, &cb_major,
			&cb_minor, &cb_build);
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to parse firmware version");
		return NULL;
	}

	/* Sanity check */
	if (cb_major == 0 || cb_version == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Invalid firmware version");
		return NULL;
	}

	return g_strdup_printf ("%u.%u.%u", cb_major, cb_minor, cb_build);
}

/* convert firmware type to user friendly string representation */
const gchar*
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
