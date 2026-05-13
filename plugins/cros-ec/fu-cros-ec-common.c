/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-struct.h"

void
fu_cros_ec_version_free(FuCrosEcVersion *version)
{
	g_free(version->boardname);
	g_free(version->triplet);
	g_free(version->sha1);
	g_free(version);
}

FuCrosEcVersion *
fu_cros_ec_version_parse(const gchar *version_raw, GError **error)
{
	gchar *ver = NULL;
	g_autofree gchar *board = g_strdup(version_raw);
	g_auto(GStrv) marker_split = NULL;
	g_auto(GStrv) triplet_split = NULL;
	g_autoptr(FuCrosEcVersion) version = g_new0(FuCrosEcVersion, 1);

	if (version_raw == NULL || strlen(version_raw) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no version string to parse");
		return NULL;
	}

	/* sample version string: cheese_v1.1.1755-4da9520 */
	ver = g_strrstr(board, "_v");
	if (ver == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "version marker not found");
		return NULL;
	}
	*ver = '\0';
	ver += 2;
	marker_split = g_strsplit_set(ver, "-+", 2);
	if (g_strv_length(marker_split) < 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "hash marker not found: %s",
			    ver);
		return NULL;
	}
	triplet_split = g_strsplit_set(marker_split[0], ".", 3);
	if (g_strv_length(triplet_split) < 3) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "improper version triplet: %s",
			    marker_split[0]);
		return NULL;
	}

	version->triplet =
	    fu_strsafe(marker_split[0], FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION);
	version->boardname = fu_strsafe(board, FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION);
	if (version->boardname == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "empty board name");
		return NULL;
	}
	version->sha1 =
	    fu_strsafe(marker_split[1], FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION);
	if (version->sha1 == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "empty SHA");
		return NULL;
	}
	version->dirty = (g_strrstr(ver, "+") != NULL);
	return g_steal_pointer(&version);
}
