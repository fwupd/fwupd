/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

gboolean	 fu_firmware_strparse_uint4_safe	(const gchar	*data,
							 gsize		 datasz,
							 gsize		 offset,
							 guint8		*value,
							 GError		**error);
gboolean	 fu_firmware_strparse_uint8_safe	(const gchar	*data,
							 gsize		 datasz,
							 gsize		 offset,
							 guint8		*value,
							 GError		**error);
gboolean	 fu_firmware_strparse_uint16_safe	(const gchar	*data,
							 gsize		 datasz,
							 gsize		 offset,
							 guint16	*value,
							 GError		**error);
gboolean	 fu_firmware_strparse_uint24_safe	(const gchar	*data,
							 gsize		 datasz,
							 gsize		 offset,
							 guint32	*value,
							 GError		**error);
gboolean	 fu_firmware_strparse_uint32_safe	(const gchar	*data,
							 gsize		 datasz,
							 gsize		 offset,
							 guint32	*value,
							 GError		**error);
