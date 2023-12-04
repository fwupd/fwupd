/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

gboolean
fu_efi_firmware_parse_sections(FuFirmware *firmware,
			       GInputStream *stream,
			       FwupdInstallFlags flags,
			       GError **error) G_GNUC_NON_NULL(1, 2);
