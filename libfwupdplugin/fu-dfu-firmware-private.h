/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-dfu-firmware.h"

guint8		 fu_dfu_firmware_get_footer_len		(FuDfuFirmware	*self);
GBytes		*fu_dfu_firmware_append_footer		(FuDfuFirmware	*self,
							 GBytes		*contents,
							 GError		**error);
gboolean	 fu_dfu_firmware_parse_footer		(FuDfuFirmware	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error);
