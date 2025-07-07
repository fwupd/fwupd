/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dfu-common.h"
#include "fu-dfu-sector.h"
#include "fu-dfu-struct.h"

#define FU_TYPE_DFU_TARGET (fu_dfu_target_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDfuTarget, fu_dfu_target, FU, DFU_TARGET, FuDevice)

struct _FuDfuTargetClass {
	FuDeviceClass parent_class;
	gboolean (*setup)(FuDfuTarget *self, GError **error);
	gboolean (*attach)(FuDfuTarget *self, FuProgress *progress, GError **error);
	gboolean (*detach)(FuDfuTarget *self, FuProgress *progress, GError **error);
	gboolean (*mass_erase)(FuDfuTarget *self, FuProgress *progress, GError **error);
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

GPtrArray *
fu_dfu_target_get_sectors(FuDfuTarget *self);
FuDfuSector *
fu_dfu_target_get_sector_default(FuDfuTarget *self);
guint8
fu_dfu_target_get_alt_setting(FuDfuTarget *self);
gboolean
fu_dfu_target_upload(FuDfuTarget *self,
		     FuFirmware *firmware,
		     FuProgress *progress,
		     FuDfuTargetTransferFlags flags,
		     GError **error);
gboolean
fu_dfu_target_setup(FuDfuTarget *self, GError **error);
gboolean
fu_dfu_target_download(FuDfuTarget *self,
		       FuFirmware *image,
		       FuProgress *progress,
		       FuDfuTargetTransferFlags flags,
		       GError **error);
