/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_RDFU_FIRMWARE (fu_logitech_rdfu_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechRdfuFirmware,
		     fu_logitech_rdfu_firmware,
		     FU,
		     LOGITECH_RDFU_FIRMWARE,
		     FuFirmware)
