/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define G_OS_INFO_KEY_NAME	     "NAME"
#define G_OS_INFO_KEY_VERSION_ID     "VERSION_ID"
#define G_OS_INFO_KEY_ID	     "ID"
#define G_OS_INFO_KEY_PRETTY_NAME    "PRETTY_NAME"
#define G_OS_INFO_KEY_BUG_REPORT_URL "BUG_REPORT_URL"

#define G_DBUS_SERVER_FLAGS_AUTHENTICATION_REQUIRE_SAME_USER (1 << 2)

static inline gchar *
g_get_os_info(const gchar *key)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *data_with_header = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autoptr(GString) str = g_string_new("[os]\n");
	if (!g_file_get_contents("/etc/os-release", &data, NULL, NULL))
		return NULL;
	g_string_append(str, data);
	if (!g_key_file_load_from_data(kf, str->str, str->len, G_KEY_FILE_NONE, NULL))
		return NULL;
	return g_key_file_get_string(kf, "os", key, NULL);
}
