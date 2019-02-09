/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-firmware.h"

G_BEGIN_DECLS

GBytes			*dfu_firmware_to_metadata	(DfuFirmware	*firmware,
							 GError		**error);
gboolean		 dfu_firmware_from_metadata	(DfuFirmware	*firmware,
							 GBytes		*bytes,
							 DfuFirmwareParseFlags flags,
							 GError		**error);

G_END_DECLS
