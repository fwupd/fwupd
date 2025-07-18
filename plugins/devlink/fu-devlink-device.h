/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

/**
 * FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME:
 *
 * Do not set the DEVLINK_ATTR_FLASH_UPDATE_COMPONENT attribute when flashing firmware.
 * This allows for firmware updates without specifying a specific component name.
 *
 * Since: 2.0.4
 */
#define FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME "omit-component-name"

#define FU_TYPE_DEVLINK_DEVICE (fu_devlink_device_get_type())
G_DECLARE_FINAL_TYPE(FuDevlinkDevice, fu_devlink_device, FU, DEVLINK_DEVICE, FuDevice)

FuDevlinkDevice *
fu_devlink_device_new(FuContext *ctx, const gchar *bus_name, const gchar *dev_name);

gboolean
fu_devlink_device_write_firmware_component(FuDevlinkDevice *self,
					   const gchar *component_name,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error);
