/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_NITROKEY_COMMON_H
#define __FU_NITROKEY_COMMON_H

#include <glib.h>

G_BEGIN_DECLS

guint32		 fu_nitrokey_perform_crc32	(const guint8	*data,
						 gsize		 size);

G_END_DECLS

#endif /* __FU_NITROKEY_COMMON_H */
