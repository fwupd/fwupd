/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-redfish-common.h"

gchar *
fu_redfish_common_buffer_to_ipv4(const guint8 *buffer)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; i < 4; i++) {
		g_string_append_printf(str, "%u", buffer[i]);
		if (i != 3)
			g_string_append(str, ".");
	}
	return g_string_free(str, FALSE);
}

gchar *
fu_redfish_common_buffer_to_ipv6(const guint8 *buffer)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; i < 16; i += 4) {
		g_string_append_printf(str,
				       "%02x%02x%02x%02x",
				       buffer[i + 0],
				       buffer[i + 1],
				       buffer[i + 2],
				       buffer[i + 3]);
		if (i != 12)
			g_string_append(str, ":");
	}
	return g_string_free(str, FALSE);
}

gchar *
fu_redfish_common_buffer_to_mac(const guint8 *buffer)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; i < 6; i++) {
		g_string_append_printf(str, "%02X", buffer[i]);
		if (i != 5)
			g_string_append(str, ":");
	}
	return g_string_free(str, FALSE);
}

gchar *
fu_redfish_common_fix_version(const gchar *version)
{
	g_auto(GStrv) split = NULL;

	g_return_val_if_fail(version != NULL, NULL);

	/* not valid */
	if (g_strcmp0(version, "-*") == 0)
		return NULL;

	/* find the section preficed with "v" */
	split = g_strsplit(version, " ", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		if (g_str_has_prefix(split[i], "v")) {
			g_debug("using %s for %s", split[i] + 1, version);
			return g_strdup(split[i] + 1);
		}
	}

	/* find the thing with dots */
	for (guint i = 0; split[i] != NULL; i++) {
		if (g_strstr_len(split[i], -1, ".")) {
			if (g_strcmp0(split[i], version) != 0)
				g_debug("using %s for %s", split[i], version);
			return g_strdup(split[i]);
		}
	}

	/* we failed to do anything clever */
	return g_strdup(version);
}

/* parses a Lenovo XCC-format version like "11A-1.02" */
gboolean
fu_redfish_common_parse_version_lenovo(const gchar *version,
				       gchar **out_build,   /* out */
				       gchar **out_version, /* out */
				       GError **error)
{
	g_auto(GStrv) versplit = g_strsplit(version, "-", -1);

	/* sanity check */
	if (g_strv_length(versplit) != 2) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not two sections");
		return FALSE;
	}
	if (strlen(versplit[0]) != 3) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid length first section");
		return FALSE;
	}

	/* milestone */
	if (!g_ascii_isdigit(versplit[0][0]) || !g_ascii_isdigit(versplit[0][1])) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "milestone number invalid");
		return FALSE;
	}

	/* build is only one letter from A -> Z */
	if (!g_ascii_isalpha(versplit[0][2])) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "build letter invalid");
		return FALSE;
	}

	/* success */
	if (out_build != NULL)
		*out_build = g_strdup(versplit[0]);
	if (out_version != NULL)
		*out_version = g_strdup(versplit[1]);
	return TRUE;
}
