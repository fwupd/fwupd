/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-common.h"

#define UDISKS_DBUS_SERVICE		 "org.freedesktop.UDisks2"
#define UDISKS_DBUS_INTERFACE_PARTITION	 "org.freedesktop.UDisks2.Partition"
#define UDISKS_DBUS_INTERFACE_FILESYSTEM "org.freedesktop.UDisks2.Filesystem"
#define UDISKS_DBUS_INTERFACE_BLOCK	 "org.freedesktop.UDisks2.Block"

GPtrArray *
fu_common_get_block_devices(GError **error);
guint64
fu_common_get_memory_size_impl(void);

/* for self tests */
const gchar *
fu_common_convert_to_gpt_type(const gchar *type);
