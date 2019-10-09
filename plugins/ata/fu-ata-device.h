/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_ATA_DEVICE (fu_ata_device_get_type ())
G_DECLARE_FINAL_TYPE (FuAtaDevice, fu_ata_device, FU, ATA_DEVICE, FuUdevDevice)

FuAtaDevice	*fu_ata_device_new_from_blob		(const guint8	*buf,
							 gsize		 sz,
							 GError		**error);

/* for self tests */
guint8		 fu_ata_device_get_transfer_mode	(FuAtaDevice	*self);
guint16		 fu_ata_device_get_transfer_blocks	(FuAtaDevice	*self);
