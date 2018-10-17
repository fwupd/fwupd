/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_COMMON_CAB_H
#define __FU_COMMON_CAB_H

#include <xmlb.h>

XbSilo		*fu_common_cab_build_silo		(GBytes		*blob,
							 guint64	 size_max,
							 GError		**error);

#endif /* __FU_COMMON_CAB_H */
