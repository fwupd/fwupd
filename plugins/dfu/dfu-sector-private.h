/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "dfu-sector.h"

G_BEGIN_DECLS

DfuSector	*dfu_sector_new		(guint32	 address,
					 guint32	 size,
					 guint32	 size_left,
					 guint16	 zone,
					 guint16	 number,
					 DfuSectorCap	 cap);

G_END_DECLS
