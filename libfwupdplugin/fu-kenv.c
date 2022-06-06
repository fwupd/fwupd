/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_KENV_H
#include <kenv.h>
#endif

#include "fwupd-error.h"

#include "fu-kenv.h"

/**
 * fu_kenv_get_string:
 * @key: a kenv key, e.g. `smbios.bios.version`
 * @error: (nullable): optional return location for an error
 *
 * Gets a BSD kernel environment string. This will not work on Linux or
 * Windows.
 *
 * Returns: (transfer full): a string, or %NULL if the @key was not found
 *
 * Since: 1.6.1
 **/
gchar *
fu_kenv_get_string(const gchar *key, GError **error)
{
#ifdef HAVE_KENV_H
	gchar buf[128] = {'\0'};

	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (kenv(KENV_GET, key, buf, sizeof(buf)) == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "cannot get kenv request for %s",
			    key);
		return NULL;
	}
	return g_strndup(buf, sizeof(buf));
#else
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "kenv not supported");
	return NULL;
#endif
}
