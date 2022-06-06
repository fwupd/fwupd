/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuSpawn"

#include "config.h"

#include "fu-release-common.h"

/**
 * fu_release_uri_get_scheme:
 * @uri: valid URI, e.g. `https://foo.bar/baz`
 *
 * Returns the USI scheme for the given URI.
 *
 * Returns: scheme value, or %NULL if invalid, e.g. `https`
 **/
gchar *
fu_release_uri_get_scheme(const gchar *uri)
{
	gchar *tmp;

	g_return_val_if_fail(uri != NULL, NULL);

	tmp = g_strstr_len(uri, -1, ":");
	if (tmp == NULL || tmp[0] == '\0')
		return NULL;
	return g_utf8_strdown(uri, tmp - uri);
}
