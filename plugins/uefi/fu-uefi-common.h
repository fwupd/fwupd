/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UEFI_COMMON_H
#define __FU_UEFI_COMMON_H

#include <glib.h>
#include <efivar.h>

G_BEGIN_DECLS

typedef struct  __attribute__((__packed__)) {
	guint16		 year;
	guint8		 month;
	guint8		 day;
	guint8		 hour;
	guint8		 minute;
	guint8		 second;
	guint8		 pad1;
	guint32		 nanosecond;
	guint16		 timezone;
	guint8		 daylight;
	guint8		 pad2;
} efi_time_t;

typedef struct  __attribute__((__packed__)) {
	guint32		 update_info_version;
	efi_guid_t	 guid;
	guint32		 capsule_flags;
	guint64		 hw_inst;
	efi_time_t	 time_attempted;
	guint32		 status;
} efi_update_info_t;

gboolean	 fu_uefi_get_bitmap_size	(const guint8	*buf,
						 gsize		 bufsz,
						 guint32	*width,
						 guint32	*height,
						 GError		**error);
gboolean	 fu_uefi_get_framebuffer_size	(guint32	*width,
						 guint32	*height,
						 GError		**error);
gboolean	 fu_uefi_secure_boot_enabled	(void);
GPtrArray	*fu_uefi_get_esrt_entry_paths	(const gchar	*esrt_path,
						 GError		**error);
guint64		 fu_uefi_read_file_as_uint64	(const gchar	*path,
						 const gchar	*attr_name);

G_END_DECLS

#endif /* __FU_UEFI_COMMON_H */
