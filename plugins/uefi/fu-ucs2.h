/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UCS2_H
#define __FU_UCS2_H

#include <glib.h>

gsize		 fu_ucs2_strlen			(const guint16	*str,
						 gssize		 limit);
guint16		*fu_uft8_to_ucs2		(const gchar	*str,
						 gssize		 max);
gchar		*fu_ucs2_to_uft8		(const guint16	*str,
						 gssize		 max);

#endif /* __FU_UCS2_H */
