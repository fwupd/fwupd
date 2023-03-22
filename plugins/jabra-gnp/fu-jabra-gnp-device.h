/*
 * Copyright (C) 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_JABRA_GNP_DEVICE (fu_jabra_gnp_device_get_type())
G_DECLARE_FINAL_TYPE(FuJabraGnpDevice, fu_jabra_gnp_device, FU, JABRA_GNP_DEVICE, FuUsbDevice)
