/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __DFU_TARGET_H
#define __DFU_TARGET_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "dfu-common.h"
#include "dfu-image.h"
#include "dfu-sector.h"

#include "fwupd-enums.h"

G_BEGIN_DECLS

#define DFU_TYPE_TARGET (dfu_target_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuTarget, dfu_target, DFU, TARGET, GUsbDevice)

/**
 * DfuTargetTransferFlags:
 * @DFU_TARGET_TRANSFER_FLAG_NONE:		No flags set
 * @DFU_TARGET_TRANSFER_FLAG_VERIFY:		Verify the download once complete
 * @DFU_TARGET_TRANSFER_FLAG_DETACH:		If required, detach from runtime mode
 * @DFU_TARGET_TRANSFER_FLAG_ATTACH:		Attach the device back to runtime after completion
 * @DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME:	Wait for runtime to load after completion
 * @DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID:	Allow downloading images with wildcard VIDs
 * @DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID:	Allow downloading images with wildcard PIDs
 * @DFU_TARGET_TRANSFER_FLAG_ANY_CIPHER:	Allow any cipher kinds to be downloaded
 * @DFU_TARGET_TRANSFER_FLAG_ADDR_HEURISTIC:	Automatically detect the address to use
 *
 * The optional flags used for transfering firmware.
 **/
typedef enum {
	DFU_TARGET_TRANSFER_FLAG_NONE		= 0,
	DFU_TARGET_TRANSFER_FLAG_VERIFY		= (1 << 0),
	DFU_TARGET_TRANSFER_FLAG_DETACH		= (1 << 1),
	DFU_TARGET_TRANSFER_FLAG_ATTACH		= (1 << 2),
	DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME	= (1 << 3),
	DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID	= (1 << 4),
	DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID	= (1 << 5),
	DFU_TARGET_TRANSFER_FLAG_ANY_CIPHER	= (1 << 6),
	DFU_TARGET_TRANSFER_FLAG_ADDR_HEURISTIC	= (1 << 7),
	/*< private >*/
	DFU_TARGET_TRANSFER_FLAG_LAST
} DfuTargetTransferFlags;

struct _DfuTargetClass
{
	GUsbDeviceClass		 parent_class;
	void			 (*percentage_changed)	(DfuTarget	*target,
							 guint		 percentage);
	void			 (*action_changed)	(DfuTarget	*target,
							 FwupdStatus	 action);
	gboolean		 (*setup)		(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
	gboolean		 (*attach)		(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
	gboolean		 (*detach)		(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
	gboolean		 (*mass_erase)		(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
	DfuElement		*(*upload_element)	(DfuTarget	*target,
							 guint32	 address,
							 gsize		 expected_size,
							 gsize		 maximum_size,
							 GCancellable	*cancellable,
							 GError		**error);
	gboolean		 (*download_element)	(DfuTarget	*target,
							 DfuElement	*element,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
};

GPtrArray	*dfu_target_get_sectors			(DfuTarget	*target);
DfuSector	*dfu_target_get_sector_default		(DfuTarget	*target);
guint8		 dfu_target_get_alt_setting		(DfuTarget	*target);
const gchar	*dfu_target_get_alt_name		(DfuTarget	*target,
							 GError		**error);
const gchar	*dfu_target_get_alt_name_for_display	(DfuTarget	*target,
							 GError		**error);
DfuImage	*dfu_target_upload			(DfuTarget	*target,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_setup			(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_download			(DfuTarget	*target,
							 DfuImage	*image,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_mass_erase			(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
DfuCipherKind	 dfu_target_get_cipher_kind		(DfuTarget	*target);

G_END_DECLS

#endif /* __DFU_TARGET_H */
