/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * SECTION:dfu-sector
 * @short_description: Object representing a sector on a chip
 *
 * This object represents an sector of memory at a specific address on the
 * device itself.
 *
 * This allows relocatable data segments to be stored in different
 * locations on the device itself.
 *
 * You can think of these objects as flash segments on devices, where a
 * complete block can be erased and then written to.
 *
 * See also: #DfuElement
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "dfu-common.h"
#include "dfu-sector.h"

typedef struct {
	guint32			 address;
	guint32			 size;
	guint32			 size_left;
	guint16			 zone;
	guint16			 number;
	DfuSectorCap	 cap;
} DfuSectorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuSector, dfu_sector, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_sector_get_instance_private (o))

static void
dfu_sector_class_init (DfuSectorClass *klass)
{
}

static void
dfu_sector_init (DfuSector *sector)
{
}

/**
 * dfu_sector_new: (skip)
 * address: the address for the sector
 * size: the size of this sector
 * size_left: the size of the rest of the sector
 * zone: the zone of memory the setor belongs
 * number: the sector number in the zone
 * cap: the #DfuSectorCap
 *
 * Creates a new DFU sector object.
 *
 * Return value: a new #DfuSector
 **/
DfuSector *
dfu_sector_new (guint32 address, guint32 size, guint32 size_left,
		guint16 zone, guint16 number, DfuSectorCap cap)
{
	DfuSectorPrivate *priv;
	DfuSector *sector;
	sector = g_object_new (DFU_TYPE_SECTOR, NULL);
	priv = GET_PRIVATE (sector);
	priv->address = address;
	priv->size = size;
	priv->size_left = size_left;
	priv->zone = zone;
	priv->number = number;
	priv->cap = cap;
	return sector;
}

/**
 * dfu_sector_get_address:
 * @sector: a #DfuSector
 *
 * Gets the alternate setting.
 *
 * Return value: integer, or 0x00 for unset
 **/
guint32
dfu_sector_get_address (DfuSector *sector)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	g_return_val_if_fail (DFU_IS_SECTOR (sector), 0x00);
	return priv->address;
}

/**
 * dfu_sector_get_size:
 * @sector: a #DfuSector
 *
 * Gets the sector size.
 *
 * Return value: integer, or 0x00 for unset
 **/
guint32
dfu_sector_get_size (DfuSector *sector)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	g_return_val_if_fail (DFU_IS_SECTOR (sector), 0x00);
	return priv->size;
}

/**
 * dfu_sector_get_size_left:
 * @sector: a #DfuSector
 *
 * Gets the size of the rest of the sector.
 *
 * Return value: integer, or 0x00 for unset
 **/
guint32
dfu_sector_get_size_left (DfuSector *sector)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	g_return_val_if_fail (DFU_IS_SECTOR (sector), 0x00);
	return priv->size_left;
}

/**
 * dfu_sector_get_zone:
 * @sector: a #DfuSector
 *
 * Gets the sector zone number.
 *
 * Return value: integer, or 0x00 for unset
 **/
guint16
dfu_sector_get_zone (DfuSector *sector)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	g_return_val_if_fail (DFU_IS_SECTOR (sector), 0x00);
	return priv->zone;
}

/**
 * dfu_sector_get_number:
 * @sector: a #DfuSector
 *
 * Gets the sector index number.
 *
 * Return value: integer, or 0x00 for unset
 **/
guint16
dfu_sector_get_number (DfuSector *sector)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	g_return_val_if_fail (DFU_IS_SECTOR (sector), 0x00);
	return priv->number;
}

/**
 * dfu_sector_get_id:
 * @sector: a #DfuSector
 *
 * Gets the sector ID which is a combination of the zone and sector number.
 * You can use this number to check if the segment is the 'same' as the last
 * written or read sector.
 *
 * Return value: integer ID, or 0x00 for unset
 **/
guint32
dfu_sector_get_id (DfuSector *sector)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	g_return_val_if_fail (DFU_IS_SECTOR (sector), 0x00);
	return (((guint32) priv->zone) << 16) | priv->number;
}

/**
 * dfu_sector_has_cap:
 * @sector: a #DfuSector
 * @cap: a #DfuSectorCap, e.g. %DFU_SECTOR_CAP_ERASEABLE
 *
 * Finds out if the sector has the required capability.
 *
 * Return value: %TRUE if the sector has the capabilily
 **/
gboolean
dfu_sector_has_cap (DfuSector *sector, DfuSectorCap cap)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	g_return_val_if_fail (DFU_IS_SECTOR (sector), FALSE);
	return (priv->cap & cap) > 0;
}

static gchar *
dfu_sector_cap_to_string (DfuSectorCap cap)
{
	GString *str = g_string_new (NULL);
	if (cap & DFU_SECTOR_CAP_READABLE)
		g_string_append (str, "R");
	if (cap & DFU_SECTOR_CAP_ERASEABLE)
		g_string_append (str, "E");
	if (cap & DFU_SECTOR_CAP_WRITEABLE)
		g_string_append (str, "W");
	return g_string_free (str, FALSE);
}

/**
 * dfu_sector_to_string:
 * @sector: a #DfuSector
 *
 * Returns a string representation of the object.
 *
 * Return value: NULL terminated string, or %NULL for invalid
 **/
gchar *
dfu_sector_to_string (DfuSector *sector)
{
	DfuSectorPrivate *priv = GET_PRIVATE (sector);
	GString *str;
	g_autofree gchar *caps_str = NULL;

	g_return_val_if_fail (DFU_IS_SECTOR (sector), NULL);

	str = g_string_new ("");
	caps_str = dfu_sector_cap_to_string (priv->cap);
	g_string_append_printf (str,
				"Zone:%i, Sec#:%i, Addr:0x%08x, "
				"Size:0x%04x, Caps:0x%01x [%s]",
				priv->zone, priv->number, priv->address,
				priv->size, priv->cap, caps_str);
	return g_string_free (str, FALSE);
}
