/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UTIL_COMMON_H__
#define __FU_UTIL_COMMON_H__

#include <glib.h>
#include <fwupd.h>

void		 fu_util_print_data		(const gchar	*title,
						 const gchar	*msg);
guint		 fu_util_prompt_for_number	(guint		 maxnum);
gboolean	 fu_util_prompt_for_boolean	(gboolean	 def);

gboolean	 fu_util_print_device_tree	(GNode *n, gpointer data);
gboolean	 fu_util_is_interesting_device	(FwupdDevice	*dev);

#endif /* __FU_UTIL_COMMON_H__ */
