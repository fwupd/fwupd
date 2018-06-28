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

#define EFI_CAPSULE_HEADER_FLAGS_PERSIST_ACROSS_RESET	0x00010000
#define EFI_CAPSULE_HEADER_FLAGS_POPULATE_SYSTEM_TABLE	0x00020000
#define EFI_CAPSULE_HEADER_FLAGS_INITIATE_RESET		0x00040000

#define EFI_UPDATE_INFO_STATUS_ATTEMPT_UPDATE		0x00000001
#define EFI_UPDATE_INFO_STATUS_ATTEMPTED		0x00000002

typedef struct __attribute__((__packed__)) {
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

typedef struct __attribute__((__packed__)) {
	efi_guid_t	 guid;
	guint32		 header_size;
	guint32		 flags;
	guint32		 capsule_image_size;
} efi_capsule_header_t;

typedef struct __attribute__((__packed__)) {
	guint8		 version;
	guint8		 checksum;
	guint8		 image_type;
	guint8		 reserved;
	guint32		 mode;
	guint32		 x_offset;
	guint32		 y_offset;
} efi_ux_capsule_header_t;

typedef struct __attribute__((__packed__)) {
	guint32		 update_info_version;
	efi_guid_t	 guid;
	guint32		 capsule_flags;
	guint64		 hw_inst;
	efi_time_t	 time_attempted;
	guint32		 status;
} efi_update_info_t;

gchar		*fu_uefi_get_esp_app_path	(const gchar	*esp_path,
						 const gchar	*cmd,
						 GError		**error);
gchar		*fu_uefi_get_built_app_path	(GError		**error);
gboolean	 fu_uefi_get_bitmap_size	(const guint8	*buf,
						 gsize		 bufsz,
						 guint32	*width,
						 guint32	*height,
						 GError		**error);
gboolean	 fu_uefi_get_framebuffer_size	(guint32	*width,
						 guint32	*height,
						 GError		**error);
gboolean	 fu_uefi_secure_boot_enabled	(void);
gchar		*fu_uefi_get_esp_path_for_os	(const gchar	*esp_path);
GPtrArray	*fu_uefi_get_esrt_entry_paths	(const gchar	*esrt_path,
						 GError		**error);
guint64		 fu_uefi_read_file_as_uint64	(const gchar	*path,
						 const gchar	*attr_name);
gboolean	 fu_uefi_prefix_efi_errors	(GError		**error);

G_END_DECLS

#endif /* __FU_UEFI_COMMON_H */
