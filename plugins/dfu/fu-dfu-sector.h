/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-dfu-struct.h"

#define FU_TYPE_DFU_SECTOR (fu_dfu_sector_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDfuSector, fu_dfu_sector, FU, DFU_SECTOR, GObject)

struct _FuDfuSectorClass {
	GObjectClass parent_class;
};

FuDfuSector *
fu_dfu_sector_new(guint32 address,
		  guint32 size,
		  guint32 size_left,
		  guint16 zone,
		  guint16 number,
		  FuDfuSectorCap cap);
guint32
fu_dfu_sector_get_id(FuDfuSector *self);
guint32
fu_dfu_sector_get_address(FuDfuSector *self);
guint32
fu_dfu_sector_get_size(FuDfuSector *self);
guint32
fu_dfu_sector_get_size_left(FuDfuSector *self);
guint16
fu_dfu_sector_get_zone(FuDfuSector *self);
gboolean
fu_dfu_sector_has_cap(FuDfuSector *self, FuDfuSectorCap cap);
gchar *
fu_dfu_sector_to_string(FuDfuSector *self);
