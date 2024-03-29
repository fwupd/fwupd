/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ATA_DEVICE (fu_ata_device_get_type())
G_DECLARE_FINAL_TYPE(FuAtaDevice, fu_ata_device, FU, ATA_DEVICE, FuUdevDevice)

FuAtaDevice *
fu_ata_device_new_from_blob(FuContext *ctx, const guint8 *buf, gsize sz, GError **error);

/* for self tests */
guint8
fu_ata_device_get_transfer_mode(FuAtaDevice *self);
guint16
fu_ata_device_get_transfer_blocks(FuAtaDevice *self);
