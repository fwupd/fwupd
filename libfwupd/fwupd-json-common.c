/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2023 Collabora Ltd.
 *    @author Frédéric Danis <frederic.danis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-json-common.h"

#if !JSON_CHECK_VERSION(1, 6, 0)
const char *
json_object_get_string_member_with_default(JsonObject *json_object,
					   const char *member_name,
					   const char *default_value)
{
	if (!json_object_has_member(json_object, member_name))
		return default_value;
	return json_object_get_string_member(json_object, member_name);
}

gint64
json_object_get_int_member_with_default(JsonObject *json_object,
					const char *member_name,
					gint64 default_value)
{
	if (!json_object_has_member(json_object, member_name))
		return default_value;
	return json_object_get_int_member(json_object, member_name);
}

gboolean
json_object_get_boolean_member_with_default(JsonObject *json_object,
					    const char *member_name,
					    gboolean default_value)
{
	if (!json_object_has_member(json_object, member_name))
		return default_value;
	return json_object_get_boolean_member(json_object, member_name);
}

#endif
