/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * This object represents an sector of memory at a specific address on the
 * device itself.
 *
 * This allows relocatable data segments to be stored in different
 * locations on the device itself.
 *
 * You can think of these objects as flash segments on devices, where a
 * complete block can be erased and then written to.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "fu-dfu-common.h"
#include "fu-dfu-sector.h"

typedef struct {
	guint32 address;
	guint32 size;
	guint32 size_left;
	guint16 zone;
	guint16 number;
	FuDfuSectorCap cap;
} FuDfuSectorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuDfuSector, fu_dfu_sector, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_dfu_sector_get_instance_private(o))

static void
fu_dfu_sector_class_init(FuDfuSectorClass *klass)
{
}

static void
fu_dfu_sector_init(FuDfuSector *self)
{
}

FuDfuSector *
fu_dfu_sector_new(guint32 address,
		  guint32 size,
		  guint32 size_left,
		  guint16 zone,
		  guint16 number,
		  FuDfuSectorCap cap)
{
	FuDfuSectorPrivate *priv;
	FuDfuSector *self;
	self = g_object_new(FU_TYPE_DFU_SECTOR, NULL);
	priv = GET_PRIVATE(self);
	priv->address = address;
	priv->size = size;
	priv->size_left = size_left;
	priv->zone = zone;
	priv->number = number;
	priv->cap = cap;
	return self;
}

guint32
fu_dfu_sector_get_address(FuDfuSector *self)
{
	FuDfuSectorPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_SECTOR(self), 0x00);
	return priv->address;
}

guint32
fu_dfu_sector_get_size(FuDfuSector *self)
{
	FuDfuSectorPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_SECTOR(self), 0x00);
	return priv->size;
}

guint16
fu_dfu_sector_get_zone(FuDfuSector *self)
{
	FuDfuSectorPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_SECTOR(self), 0x00);
	return priv->zone;
}

/* use this number to check if the segment is the 'same' as the last
 * written or read sector */
guint32
fu_dfu_sector_get_id(FuDfuSector *self)
{
	FuDfuSectorPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_SECTOR(self), 0x00);
	return (((guint32)priv->zone) << 16) | priv->number;
}

gboolean
fu_dfu_sector_has_cap(FuDfuSector *self, FuDfuSectorCap cap)
{
	FuDfuSectorPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_SECTOR(self), FALSE);
	return (priv->cap & cap) > 0;
}

gchar *
fu_dfu_sector_to_string(FuDfuSector *self)
{
	FuDfuSectorPrivate *priv = GET_PRIVATE(self);
	GString *str;
	g_autofree gchar *caps_str = NULL;

	g_return_val_if_fail(FU_IS_DFU_SECTOR(self), NULL);

	str = g_string_new("");
	caps_str = fu_dfu_sector_cap_to_string(priv->cap);
	g_string_append_printf(str,
			       "Zone:%i, Sec#:%i, Addr:0x%08x, "
			       "Size:0x%04x, Caps:0x%01x [%s]",
			       priv->zone,
			       priv->number,
			       priv->address,
			       priv->size,
			       priv->cap,
			       caps_str);
	return g_string_free(str, FALSE);
}
