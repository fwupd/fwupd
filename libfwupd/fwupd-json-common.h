/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2023 Collabora Ltd.
 *    @author Frédéric Danis <frederic.danis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#if !JSON_CHECK_VERSION(1, 6, 0)
const char *
json_object_get_string_member_with_default(JsonObject *json_object,
					   const char *member_name,
					   const char *default_value);
gint64
json_object_get_int_member_with_default(JsonObject *json_object,
					const char *member_name,
					gint64 default_value);
gboolean
json_object_get_boolean_member_with_default(JsonObject *json_object,
					    const char *member_name,
					    gboolean default_value);
#endif

G_END_DECLS
