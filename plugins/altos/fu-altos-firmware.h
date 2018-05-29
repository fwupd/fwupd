/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
