/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-firmware.h"

DfuFirmwareFormat	 dfu_firmware_detect_ihex	(GBytes		*bytes);
GBytes			*dfu_firmware_to_ihex		(DfuFirmware	*firmware,
							GError		**error);
gboolean		 dfu_firmware_from_ihex		(DfuFirmware	*firmware,
							GBytes		*bytes,
							DfuFirmwareParseFlags flags,
							GError		**error);
