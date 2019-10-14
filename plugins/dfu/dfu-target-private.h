/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gusb.h>

#include "dfu-device.h"
#include "dfu-target.h"
#include "dfu-sector.h"


DfuTarget	*dfu_target_new				(void);

GBytes		*dfu_target_upload_chunk		(DfuTarget	*target,
							 guint16	 index,
							 gsize		 buf_sz,
							 GError		**error);
gboolean	 dfu_target_download_chunk		(DfuTarget	*target,
							 guint16	 index,
							 GBytes		*bytes,
							 GError		**error);
gboolean	 dfu_target_attach			(DfuTarget	*target,
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
							 GError		**error);
DfuSector	*dfu_target_get_sector_for_addr		(DfuTarget	*target,
							 guint32	 addr);

/* export this just for the self tests */
gboolean	 dfu_target_parse_sectors		(DfuTarget	*target,
							 const gchar	*alt_name,
							 GError		**error);
