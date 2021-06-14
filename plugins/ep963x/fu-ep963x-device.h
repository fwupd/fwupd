/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_EP963X_DEVICE (fu_ep963x_device_get_type ())
G_DECLARE_FINAL_TYPE (FuEp963xDevice, fu_ep963x_device, FU, EP963X_DEVICE, FuHidDevice)
