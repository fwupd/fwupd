/*
 * Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MNTRE_DEVICE (fu_mntre_device_get_type())
G_DECLARE_FINAL_TYPE(FuMntreDevice, fu_mntre_device, FU, MNTRE_DEVICE, FuUsbDevice)
