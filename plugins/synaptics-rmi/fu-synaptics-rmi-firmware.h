/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_RMI_FIRMWARE (fu_synaptics_rmi_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsRmiFirmware,
		     fu_synaptics_rmi_firmware,
		     FU,
		     SYNAPTICS_RMI_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_synaptics_rmi_firmware_new(void);
guint32
fu_synaptics_rmi_firmware_get_sig_size(FuSynapticsRmiFirmware *self);
