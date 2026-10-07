/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#define FU_REALTEK_ALC408X_FIRMWARE_SIZE       0x10000
#define FU_REALTEK_ALC408X_FIRMWARE_HDR_OFFSET 0x1000
#define FU_REALTEK_ALC408X_CHIP		       0x4080

gchar *
fu_realtek_alc408x_version_to_string(guint64 version_raw);
