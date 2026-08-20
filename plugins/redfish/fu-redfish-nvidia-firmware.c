/*
 * Copyright 2025 prakashd <prakashd@ami.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-nvidia-firmware.h"

struct _FuRedfishNvidiaFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuRedfishNvidiaFirmware,
	      fu_redfish_nvidia_firmware,
	      FU_TYPE_FIRMWARE)

static void
fu_redfish_nvidia_firmware_init(FuRedfishNvidiaFirmware *self)
{
}

static void
fu_redfish_nvidia_firmware_class_init(FuRedfishNvidiaFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_set_size_max(firmware_class, 150 * FU_MB);
}
