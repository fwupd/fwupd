/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-error.h"

#include "fu-redfish-common.h"

GBytes *
fu_redfish_common_get_evivar_raw (efi_guid_t guid, const gchar *name, GError **error)
{
	gsize sz = 0;
	guint32 attribs = 0;
	guint8 *data = NULL;

	if (efi_get_variable (guid, name, &data, &sz, &attribs) < 0) {
		g_autofree gchar *guid_str = NULL;
		efi_guid_to_str (&guid, &guid_str);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to get efivar for %s %s",
			     guid_str, name);
		return NULL;
	}
	return g_bytes_new_take (data, sz);
}

gchar *
fu_redfish_common_buffer_to_ipv4 (const guint8 *buffer)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 4; i++) {
		g_string_append_printf (str, "%u", buffer[i]);
		if (i != 3)
			g_string_append (str, ".");
	}
	return g_string_free (str, FALSE);
}

gchar *
fu_redfish_common_buffer_to_ipv6 (const guint8 *buffer)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 16; i += 4) {
		g_string_append_printf (str, "%02x%02x%02x%02x",
					buffer[i+0], buffer[i+1],
					buffer[i+2], buffer[i+3]);
		if (i != 12)
			g_string_append (str, ":");
	}
	return g_string_free (str, FALSE);
}

/* vim: set noexpandtab: */
