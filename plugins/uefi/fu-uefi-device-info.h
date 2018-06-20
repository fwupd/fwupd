/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UEFI_DEVICE_INFO_H
#define __FU_UEFI_DEVICE_INFO_H

#include <glib-object.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

typedef struct __attribute__((__packed__)) {
	guint32 update_info_version;

	/* stuff we need to apply an update */
	efi_guid_t guid;
	guint32 capsule_flags;
	guint64 hw_inst;

	efi_time_t time_attempted;

	/* our metadata */
	guint32 status;

	/* variadic device path */
	union {
		efidp_header *dp_ptr;
		guint8 dp_buf[0]; /* only valid in get_info and put_info */
	};
} FuUefiDeviceInfo;

FuUefiDeviceInfo *fu_uefi_device_info_new		(const gchar	*guidstr,
							 guint64	 hw_inst,
							 GError		**error);
gboolean	 fu_uefi_device_info_update		(FuUefiDeviceInfo *info,
							 GError		**error);
gchar		*fu_uefi_device_info_get_media_path	(const gchar	*esp_path,
							 FuUefiDeviceInfo *info);
gboolean	 fu_uefi_device_info_set_device_path	(FuUefiDeviceInfo *info,
							 const gchar	*path,
							 GError		**error);
void		 fu_uefi_device_info_free		(FuUefiDeviceInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUefiDeviceInfo, fu_uefi_device_info_free);

G_END_DECLS

#endif /* __FU_UEFI_DEVICE_INFO_H */
