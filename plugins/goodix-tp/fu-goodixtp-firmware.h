/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GOODIXTP_FIRMWARE (fu_goodixtp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuGoodixtpFirmware, fu_goodixtp_firmware, FU, GOODIXTP_FIRMWARE, FuFirmware)

FuFirmware *
fu_goodixtp_firmware_new(void);

gint
fu_goodixtp_firmware_get_version(FuGoodixtpFirmware *self);

gboolean
fu_goodixtp_firmware_has_config(FuGoodixtpFirmware *self);

guint8 *
fu_goodixtp_firmware_get_data(FuGoodixtpFirmware *self);

gint
fu_goodixtp_firmware_get_len(FuGoodixtpFirmware *self);

guint32
fu_goodixtp_firmware_get_addr(FuGoodixtpFirmware *self, gint index);

gboolean
fu_goodixtp_frmware_parse(FuFirmware *firmware,
			  GBytes *fw,
			  gint ic_type,
			  guint8 sensor_id,
			  GError **error);
