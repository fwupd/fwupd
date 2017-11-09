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

#ifndef __DFU_SECTOR_H
#define __DFU_SECTOR_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define DFU_TYPE_SECTOR (dfu_sector_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuSector, dfu_sector, DFU, SECTOR, GObject)

struct _DfuSectorClass
{
	GObjectClass		 parent_class;
};

/**
 * DfuSectorCap:
 * @DFU_SECTOR_CAP_NONE:		No operations possible
 * @DFU_SECTOR_CAP_READABLE:		Sector can be read
 * @DFU_SECTOR_CAP_WRITEABLE:		Sector can be written
 * @DFU_SECTOR_CAP_ERASEABLE:		Sector can be erased
 *
 * The flags indicating what the sector can do.
 **/
typedef enum {
	DFU_SECTOR_CAP_NONE		= 0,
	DFU_SECTOR_CAP_READABLE		= 1 << 0,
	DFU_SECTOR_CAP_WRITEABLE	= 1 << 1,
	DFU_SECTOR_CAP_ERASEABLE	= 1 << 2,
	/*< private >*/
	DFU_SECTOR_CAP_LAST
} DfuSectorCap;

guint32		 dfu_sector_get_id		(DfuSector	*sector);
guint32		 dfu_sector_get_address		(DfuSector	*sector);
guint32		 dfu_sector_get_size		(DfuSector	*sector);
guint32		 dfu_sector_get_size_left	(DfuSector	*sector);
guint16		 dfu_sector_get_zone		(DfuSector	*sector);
guint16		 dfu_sector_get_number		(DfuSector	*sector);
gboolean	 dfu_sector_has_cap		(DfuSector	*sector,
						 DfuSectorCap	 cap);
gchar		*dfu_sector_to_string		(DfuSector	*sector);

G_END_DECLS

#endif /* __DFU_SECTOR_H */
