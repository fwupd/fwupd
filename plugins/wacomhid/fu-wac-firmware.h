/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_WAC_FIRMWARE_H
#define __FU_WAC_FIRMWARE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-firmware.h"

G_BEGIN_DECLS

gboolean	 fu_wac_firmware_parse_data	(DfuFirmware	*firmware,
						 GBytes		*bytes,
						 DfuFirmwareParseFlags flags,
						 GError		**error);

G_END_DECLS

#endif /* __FU_WAC_FIRMWARE_H */
