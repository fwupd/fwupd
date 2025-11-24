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
 * Since: 2.1.0
 **/
void
fwupd_json_indent(GString *str, guint depth)
{
	for (guint i = 0; i < 2 * depth; i++)
		g_string_append(str, " ");
}

/**
 * fwupd_json_node_kind_to_string:
 * @node_kind: a #FwupdJsonNodeKind, e.g. %FWUPD_JSON_NODE_KIND_STRING
 *
 * Converts an enumerated node kind to a string.
 *
 * Returns: string
 *
 * Since: 2.1.0
 **/
const gchar *
fwupd_json_node_kind_to_string(FwupdJsonNodeKind node_kind)
{
	if (node_kind == FWUPD_JSON_NODE_KIND_RAW)
		return "raw";
	if (node_kind == FWUPD_JSON_NODE_KIND_STRING)
		return "string";
	if (node_kind == FWUPD_JSON_NODE_KIND_ARRAY)
		return "array";
	if (node_kind == FWUPD_JSON_NODE_KIND_OBJECT)
		return "object";
	return NULL;
}
