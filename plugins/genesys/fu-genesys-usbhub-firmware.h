/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_USBHUB_FIRMWARE (fu_genesys_usbhub_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysUsbhubFirmware,
		     fu_genesys_usbhub_firmware,
		     FU,
		     GENESYS_USBHUB_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_genesys_usbhub_firmware_new(void);
gboolean
fu_genesys_usbhub_firmware_verify_checksum(GBytes *fw, GError **error);
gboolean
fu_genesys_usbhub_firmware_calculate_size(GBytes *fw, gsize offset, gsize *size, GError **error);
gboolean
fu_genesys_usbhub_firmware_ensure_version(FuFirmware *firmware, GError **error);
