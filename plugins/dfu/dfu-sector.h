/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

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

DfuSector	*dfu_sector_new			(guint32	 address,
						 guint32	 size,
						 guint32	 size_left,
						 guint16	 zone,
						 guint16	 number,
						 DfuSectorCap	 cap);
guint32		 dfu_sector_get_id		(DfuSector	*sector);
guint32		 dfu_sector_get_address		(DfuSector	*sector);
guint32		 dfu_sector_get_size		(DfuSector	*sector);
guint32		 dfu_sector_get_size_left	(DfuSector	*sector);
guint16		 dfu_sector_get_zone		(DfuSector	*sector);
guint16		 dfu_sector_get_number		(DfuSector	*sector);
gboolean	 dfu_sector_has_cap		(DfuSector	*sector,
						 DfuSectorCap	 cap);
gchar		*dfu_sector_to_string		(DfuSector	*sector);
