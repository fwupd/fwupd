/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __FU_ALTOS_FIRMWARE_H
#define __FU_ALTOS_FIRMWARE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_TYPE_ALTOS_FIRMWARE (fu_altos_firmware_get_type ())

G_DECLARE_FINAL_TYPE (FuAltosFirmware, fu_altos_firmware, FU, ALTOS_FIRMWARE, GObject)

FuAltosFirmware	*fu_altos_firmware_new		(void);
GBytes		*fu_altos_firmware_get_data	(FuAltosFirmware	*self);
guint64		 fu_altos_firmware_get_address	(FuAltosFirmware	*self);
gboolean	 fu_altos_firmware_parse	(FuAltosFirmware	*self,
						 GBytes			*blob,
						 GError			**error);

G_END_DECLS

#endif /* __FU_ALTOS_FIRMWARE_H */
