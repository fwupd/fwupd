/*
 * Copyright 2026 Marc-Antoine Perennou <ma.perennou@criteo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REDFISH_FIRMWARE (fu_redfish_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishFirmware, fu_redfish_firmware, FU, REDFISH_FIRMWARE, FuFirmware)
