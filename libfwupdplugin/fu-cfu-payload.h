/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CFU_PAYLOAD (fu_cfu_payload_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCfuPayload, fu_cfu_payload, FU, CFU_PAYLOAD, FuFirmware)

struct _FuCfuPayloadClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_cfu_payload_new(void);
