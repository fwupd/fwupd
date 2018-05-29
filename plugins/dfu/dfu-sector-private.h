/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __DFU_SECTOR_PRIVATE_H
#define __DFU_SECTOR_PRIVATE_H

#include "dfu-sector.h"

G_BEGIN_DECLS

DfuSector	*dfu_sector_new		(guint32	 address,
					 guint32	 size,
					 guint32	 size_left,
					 guint16	 zone,
					 guint16	 number,
					 DfuSectorCap	 cap);

G_END_DECLS

#endif /* __DFU_SECTOR_PRIVATE_H */
