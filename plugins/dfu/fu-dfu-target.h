/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "fu-dfu-common.h"
#include "fu-dfu-sector.h"

#define FU_TYPE_DFU_TARGET (fu_dfu_target_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDfuTarget, fu_dfu_target, FU, DFU_TARGET, GUsbDevice)

/**
 * FuDfuTargetTransferFlags:
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
} FuDfuTargetTransferFlags;

struct _FuDfuTargetClass
{
	GUsbDeviceClass		 parent_class;
	void			 (*action_changed)	(FuDfuTarget	*self,
							 FwupdStatus	 action);
	gboolean		 (*setup)		(FuDfuTarget	*self,
							 GError		**error);
	gboolean		 (*attach)		(FuDfuTarget	*self,
							 GError		**error);
	gboolean		 (*detach)		(FuDfuTarget	*self,
							 GError		**error);
	gboolean		 (*mass_erase)		(FuDfuTarget	*self,
							 GError		**error);
	FuChunk *(*upload_element)(FuDfuTarget *self,
				   guint32 address,
				   gsize expected_size,
				   gsize maximum_size,
				   FuProgress *progress,
				   GError **error);
	gboolean (*download_element)(FuDfuTarget *self,
				     FuChunk *chk,
				     FuProgress *progress,
				     FuDfuTargetTransferFlags flags,
				     GError **error);
};

GPtrArray	*fu_dfu_target_get_sectors		(FuDfuTarget	*self);
FuDfuSector	*fu_dfu_target_get_sector_default	(FuDfuTarget	*self);
guint8		 fu_dfu_target_get_alt_setting		(FuDfuTarget	*self);
const gchar	*fu_dfu_target_get_alt_name		(FuDfuTarget	*self,
							 GError		**error);
const gchar	*fu_dfu_target_get_alt_name_for_display	(FuDfuTarget	*self,
							 GError		**error);
gboolean
fu_dfu_target_upload(FuDfuTarget *self,
		     FuFirmware *firmware,
		     FuProgress *progress,
		     FuDfuTargetTransferFlags flags,
		     GError **error);
gboolean	 fu_dfu_target_setup			(FuDfuTarget	*self,
							 GError		**error);
gboolean
fu_dfu_target_download(FuDfuTarget *self,
		       FuFirmware *image,
		       FuProgress *progress,
		       FuDfuTargetTransferFlags flags,
		       GError **error);
gboolean	 fu_dfu_target_mass_erase		(FuDfuTarget	*self,
							 GError		**error);
void		 fu_dfu_target_to_string		(FuDfuTarget	*self,
							 guint		 idt,
							 GString	*str);
