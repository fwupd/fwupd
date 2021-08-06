/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gusb.h>

#include "fu-dfu-device.h"
#include "fu-dfu-target.h"
#include "fu-dfu-sector.h"

FuDfuTarget	*fu_dfu_target_new			(void);

GBytes		*fu_dfu_target_upload_chunk		(FuDfuTarget	*self,
							 guint16	 index,
							 gsize		 buf_sz,
							 GError		**error);
gboolean	 fu_dfu_target_download_chunk		(FuDfuTarget	*self,
							 guint16	 index,
							 GBytes		*bytes,
							 GError		**error);
gboolean	 fu_dfu_target_attach			(FuDfuTarget	*self,
							 GError		**error);
void		 fu_dfu_target_set_alt_idx		(FuDfuTarget	*self,
							 guint8		 alt_idx);
void		 fu_dfu_target_set_alt_setting		(FuDfuTarget	*self,
							 guint8		 alt_setting);

/* for the other implementations */
void		 fu_dfu_target_set_action		(FuDfuTarget	*self,
							 FwupdStatus	 action);
void		 fu_dfu_target_set_alt_name		(FuDfuTarget	*self,
							 const gchar	*alt_name);
void		 fu_dfu_target_set_device		(FuDfuTarget	*self,
							 FuDfuDevice	*device);
FuDfuDevice	*fu_dfu_target_get_device		(FuDfuTarget	*self);
gboolean	 fu_dfu_target_check_status		(FuDfuTarget	*self,
							 GError		**error);
FuDfuSector	*fu_dfu_target_get_sector_for_addr	(FuDfuTarget	*self,
							 guint32	 addr);

/* export this just for the self tests */
gboolean	 fu_dfu_target_parse_sectors		(FuDfuTarget	*self,
							 const gchar	*alt_name,
							 GError		**error);
