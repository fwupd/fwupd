/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __DFU_FORMAT_METADATA_H
#define __DFU_FORMAT_METADATA_H

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

#endif /* __DFU_FORMAT_METADATA_H */
