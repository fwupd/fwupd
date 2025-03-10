/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libmbim-glib.h>

MbimDevice *
_mbim_device_new_sync(GFile *file, guint timeout_ms, GError **error); /* nocheck:name */
gboolean
_mbim_device_open_sync(MbimDevice *mbim_device, /* nocheck:name */
		       guint timeout_ms,
		       GError **error);
gboolean
_mbim_device_close_sync(MbimDevice *mbim_device, /* nocheck:name */
			guint timeout_ms,
			GError **error);
