/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

gchar		*fu_kenv_get_string		(const gchar	*key,
						 GError		**error);
