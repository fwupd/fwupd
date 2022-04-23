/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-common.h"

gboolean
fu_cros_ec_parse_version(const gchar *version_raw, struct cros_ec_version *version, GError **error)
{
	g_auto(GStrv) v_split = NULL;
	g_auto(GStrv) marker_split = NULL;
	g_auto(GStrv) triplet_split = NULL;

	if (NULL == version_raw || 0 == strlen(version_raw)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no version string to parse");
		return FALSE;
	}

	/* sample version string: cheese_v1.1.1755-4da9520 */
	v_split = g_strsplit(version_raw, "_v", 2);
	if (g_strv_length(v_split) < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "version marker not found");
		return FALSE;
	}
	marker_split = g_strsplit_set(v_split[1], "-+", 2);
	if (g_strv_length(marker_split) < 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "hash marker not found: %s",
			    v_split[1]);
		return FALSE;
	}
	triplet_split = g_strsplit_set(marker_split[0], ".", 3);
	if (g_strv_length(triplet_split) < 3) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "improper version triplet: %s",
			    marker_split[0]);
		return FALSE;
	}
	(void)g_strlcpy(version->triplet, marker_split[0], 32);
	if (g_strlcpy(version->boardname, v_split[0], 32) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "empty board name");
		return FALSE;
	}
	if (g_strlcpy(version->sha1, marker_split[1], 32) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "empty SHA");
		return FALSE;
	}
	version->dirty = (g_strrstr(v_split[1], "+") != NULL);

	return TRUE;
}
