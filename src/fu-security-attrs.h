/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

gchar		*fu_security_attrs_calculate_hsi	(GPtrArray	*attrs);
void		 fu_security_attrs_depsolve		(GPtrArray	*attrs);
