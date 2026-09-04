/*
 * Copyright 2025 prakashd <prakashd@ami.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-ami-firmware.h"

struct _FuRedfishAmiFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuRedfishAmiFirmware, fu_redfish_ami_firmware, FU_TYPE_FIRMWARE)

static void
fu_redfish_ami_firmware_init(FuRedfishAmiFirmware *self)
{
}

static void
fu_redfish_ami_firmware_class_init(FuRedfishAmiFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_set_size_max(firmware_class, 150 * FU_MB);
}
