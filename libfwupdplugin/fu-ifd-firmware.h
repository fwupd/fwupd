/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_IFD_FIRMWARE (fu_ifd_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIfdFirmware, fu_ifd_firmware, FU, IFD_FIRMWARE, FuFirmware)

struct _FuIfdFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_ifd_firmware_new(void);
gboolean
fu_ifd_firmware_check_jedec_cmd(FuIfdFirmware *self, guint8 cmd);
