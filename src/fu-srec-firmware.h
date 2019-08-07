/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

G_BEGIN_DECLS

#define FU_TYPE_SREC_FIRMWARE (fu_srec_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuSrecFirmware, fu_srec_firmware, FU, SREC_FIRMWARE, FuFirmware)

FuFirmware	*fu_srec_firmware_new		(void);

G_END_DECLS
