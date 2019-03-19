/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
	FU_APP_FLAGS_NONE		= 0,
	FU_APP_FLAGS_NO_IDLE_SOURCES	= 1 << 0,
	FU_APP_FLAGS_LAST
} FuAppFlags;

typedef enum {
	FU_DUMP_FLAGS_NONE		= 0,
	FU_DUMP_FLAGS_SHOW_ASCII	= 1 << 0,
	FU_DUMP_FLAGS_SHOW_ADDRESSES	= 1 << 1,
	FU_DUMP_FLAGS_LAST
} FuDumpFlags;

typedef enum {
	FU_PATH_KIND_CACHEDIR_PKG,
	FU_PATH_KIND_DATADIR_PKG,
	FU_PATH_KIND_EFIAPPDIR,
	FU_PATH_KIND_LOCALSTATEDIR,
	FU_PATH_KIND_LOCALSTATEDIR_PKG,
	FU_PATH_KIND_PLUGINDIR_PKG,
	FU_PATH_KIND_SYSCONFDIR,
	FU_PATH_KIND_SYSCONFDIR_PKG,
	FU_PATH_KIND_SYSFSDIR_FW,
	FU_PATH_KIND_SYSFSDIR_DRIVERS,
	FU_PATH_KIND_SYSFSDIR_TPM,
	FU_PATH_KIND_LAST
} FuPathKind;

typedef void	(*FuOutputHandler)		(const gchar	*line,
						 gpointer	 user_data);

gboolean	 fu_common_spawn_sync		(const gchar * const *argv,
						 FuOutputHandler handler_cb,
						 gpointer	 handler_user_data,
						 guint		 timeout_ms,
						 GCancellable	*cancellable,
						 GError		**error);

gchar		*fu_common_get_path		(FuPathKind	 path_kind);
gboolean	 fu_common_rmtree		(const gchar	*directory,
						 GError		**error);
GPtrArray	*fu_common_get_files_recursive	(const gchar	*path,
						 GError		**error);
gboolean	 fu_common_mkdir_parent		(const gchar	*filename,
						 GError		**error);
gboolean	 fu_common_set_contents_bytes	(const gchar	*filename,
						 GBytes		*bytes,
						 GError		**error);
GBytes		*fu_common_get_contents_bytes	(const gchar	*filename,
						 GError		**error);
GBytes		*fu_common_get_contents_fd	(gint		 fd,
						 gsize		 count,
						 GError		**error);
gboolean	 fu_common_extract_archive	(GBytes		*blob,
						 const gchar	*dir,
						 GError		**error);
GBytes		*fu_common_firmware_builder	(GBytes		*bytes,
						 const gchar	*script_fn,
						 const gchar	*output_fn,
						 GError		**error);
GError		*fu_common_error_array_get_best	(GPtrArray	*errors);
guint64		 fu_common_strtoull		(const gchar	*str);
gchar		*fu_common_find_program_in_path	(const gchar	*basename,
						 GError		**error);
gchar		*fu_common_strstrip		(const gchar	*str);
void		 fu_common_dump_raw		(const gchar	*log_domain,
						 const gchar	*title,
						 const guint8	*data,
						 gsize		 len);
void		 fu_common_dump_full		(const gchar	*log_domain,
						 const gchar	*title,
						 const guint8	*data,
						 gsize		 len,
						 guint		 columns,
						 FuDumpFlags	 flags);
void		 fu_common_dump_bytes		(const gchar	*log_domain,
						 const gchar	*title,
						 GBytes		*bytes);
GBytes		*fu_common_bytes_align		(GBytes		*bytes,
						 gsize		 blksz,
						 gchar		 padval);
gboolean	 fu_common_bytes_is_empty	(GBytes		*bytes);

typedef guint FuEndianType;

void		 fu_common_write_uint16		(guint8		*buf,
						 guint16	 val_native,
						 FuEndianType	 endian);
void		 fu_common_write_uint32		(guint8		*buf,
						 guint32	 val_native,
						 FuEndianType	 endian);
guint16		 fu_common_read_uint16		(const guint8	*buf,
						 FuEndianType	 endian);
guint32		 fu_common_read_uint32		(const guint8	*buf,
						 FuEndianType	 endian);

guint		 fu_common_string_replace	(GString	*string,
						 const gchar	*search,
						 const gchar	*replace);

G_END_DECLS
