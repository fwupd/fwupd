/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_COMMON_CAB_H
#define __FU_COMMON_CAB_H

#include <appstream-glib.h>

AsStore		*fu_common_store_from_cab_bytes		(GBytes		*blob,
							 guint64	 size_max,
							 GError		**error);

#endif /* __FU_COMMON_CAB_H */
