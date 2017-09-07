/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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
