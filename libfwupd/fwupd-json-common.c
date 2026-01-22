/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-json-common-private.h"

/**
 * fwupd_json_indent:
 * @str: a #GString
 * @depth: function depth
 *
 * Indent the string by the indent depth.
 *
 * Since: 2.1.1
 **/
void
fwupd_json_indent(GString *str, guint depth)
{
	for (guint i = 0; i < 2 * depth; i++)
		g_string_append(str, " ");
}
