/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPROM_FIRMWARE (fu_synaprom_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuSynapromFirmware, fu_synaprom_firmware, FU, SYNAPROM_FIRMWARE, FuFirmware)

FuFirmware *
fu_synaprom_firmware_new(void);
guint32
fu_synaprom_firmware_get_product_id(FuSynapromFirmware *self);
