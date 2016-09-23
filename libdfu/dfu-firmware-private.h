/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __DFU_FIRMWARE_PRIVATE_H
#define __DFU_FIRMWARE_PRIVATE_H

#include "dfu-firmware.h"

G_BEGIN_DECLS

void		 dfu_firmware_add_symbol	(DfuFirmware	*firmware,
						 const gchar	*symbol_name,
						 guint32	 symbol_addr);
guint32		 dfu_firmware_lookup_symbol	(DfuFirmware	*firmware,
						 const gchar	*symbol_name);
GPtrArray	*dfu_firmware_get_symbols	(DfuFirmware	*firmware);

G_END_DECLS

#endif /* __DFU_FIRMWARE_PRIVATE_H */
