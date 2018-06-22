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

typedef struct {
	guint16 year;
	guint8 month;
	guint8 day;
	guint8 hour;
	guint8 minute;
	guint8 second;
	guint8 pad1;
	guint32 nanosecond;
	guint16 timezone;
	guint8 daylight;
	guint8 pad2;
} efi_time_t;
#define EFI_TIME efi_time_t

#include "efi/fwup-efi.h"

G_BEGIN_DECLS

#define FWUPDATE_GUID EFI_GUID(0x0abba7dc,0xe516,0x4167,0xbbf5,0x4d,0x9d,0x1c,0x73,0x94,0x16)

#define CAPSULE_FLAGS_PERSIST_ACROSS_RESET	0x00010000
#define CAPSULE_FLAGS_POPULATE_SYSTEM_TABLE	0x00020000
#define CAPSULE_FLAGS_INITIATE_RESET		0x00040000

typedef struct {
       efi_guid_t guid;
       guint32 header_size;
       guint32 flags;
       guint32 capsule_image_size;
} efi_capsule_header_t;

gchar *		fu_uefi_bootmgr_get_esp_app_path (const gchar *esp_mountpoint,
						  const gchar *cmd);

gchar *		fu_uefi_bootmgr_get_source_path	 (void);

gboolean	 fu_uefi_get_bitmap_size	(const guint8	*buf,
						 gsize		 bufsz,
						 guint32	*width,
						 guint32	*height,
						 GError		**error);
gboolean	 fu_uefi_get_framebuffer_size	(guint32	*width,
						 guint32	*height,
						 GError		**error);
gboolean	 fu_uefi_secure_boot_enabled	(void);
gchar *		 fu_uefi_get_full_esp_path	(const gchar	*esp_mount);
GPtrArray	*fu_uefi_get_esrt_entry_paths	(const gchar	*esrt_path,
						 GError		**error);
guint64		 fu_uefi_read_file_as_uint64	(const gchar	*path,
						 const gchar	*attr_name);
gboolean	 fu_uefi_prefix_efi_errors	(GError		**error);

G_END_DECLS

#endif /* __FU_UEFI_COMMON_H */
