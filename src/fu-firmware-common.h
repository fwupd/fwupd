/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

guint8		 fu_firmware_strparse_uint4		(const gchar	*data);
guint8		 fu_firmware_strparse_uint8		(const gchar	*data);
guint16		 fu_firmware_strparse_uint16		(const gchar	*data);
guint32		 fu_firmware_strparse_uint24		(const gchar	*data);
guint32		 fu_firmware_strparse_uint32		(const gchar	*data);
