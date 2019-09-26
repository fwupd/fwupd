/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-firmware.h"

G_BEGIN_DECLS

DfuFirmwareFormat	 dfu_firmware_detect_srec	(GBytes		*bytes);
GBytes			*dfu_firmware_to_srec		(DfuFirmware	*firmware,
							 GError		**error);
gboolean		 dfu_firmware_from_srec		(DfuFirmware	*firmware,
							 GBytes		*bytes,
							 DfuFirmwareParseFlags flags,
							 GError		**error);

G_END_DECLS
