/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "dfu-common.h"
#include "dfu-image.h"
#include "dfu-sector.h"

#include "fwupd-enums.h"

#define DFU_TYPE_TARGET (dfu_target_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuTarget, dfu_target, DFU, TARGET, GUsbDevice)

/**
 * DfuTargetTransferFlags:
 * @DFU_TARGET_TRANSFER_FLAG_NONE:		No flags set
 * @DFU_TARGET_TRANSFER_FLAG_VERIFY:		Verify the download once complete
 * @DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID:	Allow downloading images with wildcard VIDs
 * @DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID:	Allow downloading images with wildcard PIDs
 * @DFU_TARGET_TRANSFER_FLAG_ADDR_HEURISTIC:	Automatically detect the address to use
 *
 * The optional flags used for transferring firmware.
 **/
typedef enum {
	DFU_TARGET_TRANSFER_FLAG_NONE		= 0,
	DFU_TARGET_TRANSFER_FLAG_VERIFY		= (1 << 0),
	DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID	= (1 << 4),
	DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID	= (1 << 5),
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
							 GError		**error);
	gboolean		 (*attach)		(DfuTarget	*target,
							 GError		**error);
	gboolean		 (*detach)		(DfuTarget	*target,
							 GError		**error);
	gboolean		 (*mass_erase)		(DfuTarget	*target,
							 GError		**error);
	DfuElement		*(*upload_element)	(DfuTarget	*target,
							 guint32	 address,
							 gsize		 expected_size,
							 gsize		 maximum_size,
							 GError		**error);
	gboolean		 (*download_element)	(DfuTarget	*target,
							 DfuElement	*element,
							 DfuTargetTransferFlags flags,
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
							 GError		**error);
gboolean	 dfu_target_setup			(DfuTarget	*target,
							 GError		**error);
gboolean	 dfu_target_download			(DfuTarget	*target,
							 DfuImage	*image,
							 DfuTargetTransferFlags flags,
							 GError		**error);
gboolean	 dfu_target_mass_erase			(DfuTarget	*target,
							 GError		**error);
