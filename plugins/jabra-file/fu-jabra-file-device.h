/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_JABRA_FILE_DEVICE (fu_jabra_file_device_get_type())
G_DECLARE_FINAL_TYPE(FuJabraFileDevice, fu_jabra_file_device, FU, JABRA_FILE_DEVICE, FuUsbDevice)
