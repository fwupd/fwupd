/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define FU_UEFI_VARS_GUID_EFI_GLOBAL			"8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define FU_UEFI_VARS_GUID_FWUPDATE			"0abba7dc-e516-4167-bbf5-4d9d1c739416"
#define FU_UEFI_VARS_GUID_UX_CAPSULE			"3b8c8162-188c-46a4-aec9-be43f1d65697"

#define FU_UEFI_VARS_ATTR_NON_VOLATILE			(1 << 0)
#define FU_UEFI_VARS_ATTR_BOOTSERVICE_ACCESS		(1 << 1)
#define FU_UEFI_VARS_ATTR_RUNTIME_ACCESS		(1 << 2)
#define FU_UEFI_VARS_ATTR_HARDWARE_ERROR_RECORD		(1 << 3)
#define FU_UEFI_VARS_ATTR_AUTHENTICATED_WRITE_ACCESS	(1 << 4)
#define FU_UEFI_VARS_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS (5 << 0)
#define FU_UEFI_VARS_ATTR_APPEND_WRITE			(1 << 6)

gboolean	 fu_uefi_vars_supported		(GError		**error);
gboolean	 fu_uefi_vars_exists		(const gchar	*guid,
						 const gchar	*name);
gboolean	 fu_uefi_vars_get_data		(const gchar	*guid,
						 const gchar	*name,
						 guint8		**data,
						 gsize		*data_sz,
						 guint32	*attr,
						 GError		**error);
gboolean	 fu_uefi_vars_set_data		(const gchar	*guid,
						 const gchar	*name,
						 const guint8	*data,
						 gsize		 sz,
						 guint32	 attr,
						 GError		**error);
gboolean	 fu_uefi_vars_delete		(const gchar	*guid,
						 const gchar	*name,
						 GError		**error);
gboolean	 fu_uefi_vars_delete_with_glob	(const gchar	*guid,
						 const gchar	*name_glob,
						 GError		**error);
