/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_JABRA_FILE_FIRMWARE (fu_jabra_file_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuJabraFileFirmware,
		     fu_jabra_file_firmware,
		     FU,
		     JABRA_FILE_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_jabra_file_firmware_new(void);
guint16
fu_jabra_file_firmware_get_dfu_pid(FuJabraFileFirmware *self);
