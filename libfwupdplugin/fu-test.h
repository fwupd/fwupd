/*
 * Copyright (C) 2010-2011 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

gchar		*fu_test_get_filename		(const gchar	*testdatadirs,
						 const gchar	*filename);
gboolean	 fu_test_compare_lines		(const gchar	*txt1,
						 const gchar	*txt2,
						 GError		**error);
