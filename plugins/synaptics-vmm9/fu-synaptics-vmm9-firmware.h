/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_VMM9_FIRMWARE (fu_synaptics_vmm9_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsVmm9Firmware,
		     fu_synaptics_vmm9_firmware,
		     FU,
		     SYNAPTICS_VMM9_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_synaptics_vmm9_firmware_new(void);
guint8
fu_synaptics_vmm9_firmware_get_board_id(FuSynapticsVmm9Firmware *self);
guint8
fu_synaptics_vmm9_firmware_get_customer_id(FuSynapticsVmm9Firmware *self);
