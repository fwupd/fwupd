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

#ifndef __DFU_TARGET_PRIVATE_H
#define __DFU_TARGET_PRIVATE_H

#include <gusb.h>

#include "dfu-device.h"
#include "dfu-target.h"
#include "dfu-sector.h"

G_BEGIN_DECLS

DfuTarget	*dfu_target_new				(void);

GBytes		*dfu_target_upload_chunk		(DfuTarget	*target,
							 guint16	 index,
							 gsize		 buf_sz,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_download_chunk		(DfuTarget	*target,
							 guint16	 index,
							 GBytes		*bytes,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_attach			(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
void		 dfu_target_set_alt_idx			(DfuTarget	*target,
							 guint8		 alt_idx);
void		 dfu_target_set_alt_setting		(DfuTarget	*target,
							 guint8		 alt_setting);

/* for the other implementations */
void		 dfu_target_set_action			(DfuTarget	*target,
							 FwupdStatus	 action);
void		 dfu_target_set_percentage_raw		(DfuTarget	*target,
							 guint		 percentage);
void		 dfu_target_set_percentage		(DfuTarget	*target,
							 guint		 value,
							 guint		 total);
void		 dfu_target_set_alt_name		(DfuTarget	*target,
							 const gchar	*alt_name);
void		 dfu_target_set_device			(DfuTarget	*target,
							 DfuDevice	*device);
DfuDevice	*dfu_target_get_device			(DfuTarget	*target);
gboolean	 dfu_target_check_status		(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
DfuSector	*dfu_target_get_sector_for_addr		(DfuTarget	*target,
							 guint32	 addr);

/* export this just for the self tests */
gboolean	 dfu_target_parse_sectors		(DfuTarget	*target,
							 const gchar	*alt_name,
							 GError		**error);

G_END_DECLS

#endif /* __DFU_TARGET_PRIVATE_H */
