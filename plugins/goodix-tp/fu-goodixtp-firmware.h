/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GOODIXTP_FIRMWARE (fu_goodixtp_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuGoodixtpFirmware,
			 fu_goodixtp_firmware,
			 FU,
			 GOODIXTP_FIRMWARE,
			 FuFirmware)

struct _FuGoodixtpFirmwareClass {
	FuFirmwareClass parent_class;
};

guint32
fu_goodixtp_firmware_get_version(FuGoodixtpFirmware *self);

void
fu_goodixtp_firmware_set_version(FuGoodixtpFirmware *self, guint32 version);

guint8 *
fu_goodixtp_firmware_get_data(FuGoodixtpFirmware *self);

gint
fu_goodixtp_firmware_get_len(FuGoodixtpFirmware *self);

guint32
fu_goodixtp_firmware_get_addr(FuGoodixtpFirmware *self, gint index);

void
fu_goodixtp_add_chunk_data(FuGoodixtpFirmware *self,
			   guint8 type,
			   guint32 addr,
			   guint8 *data,
			   gint dataLen);
