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

G_BEGIN_DECLS

DfuTarget	*dfu_target_new				(DfuDevice	*device,
							 GUsbInterface	*iface);

GBytes		*dfu_target_upload_chunk		(DfuTarget	*target,
							 guint8		 index,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_download_chunk		(DfuTarget	*target,
							 guint8		 index,
							 GBytes		*bytes,
							 GCancellable	*cancellable,
							 GError		**error);

/* export this just for the self tests */
gboolean	 dfu_target_parse_sectors		(DfuTarget	*target,
							 const gchar	*alt_name,
							 GError		**error);

G_END_DECLS

#endif /* __DFU_TARGET_PRIVATE_H */
