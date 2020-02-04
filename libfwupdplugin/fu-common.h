/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

/**
 * FuAppFlags:
 * @FU_APP_FLAGS_NONE:			No flags set
 * @FU_APP_FLAGS_NO_IDLE_SOURCES:	Disallow idle sources
 *
 * The flags to use when loading an application.
 **/
typedef enum {
	FU_APP_FLAGS_NONE		= 0,
	FU_APP_FLAGS_NO_IDLE_SOURCES	= 1 << 0,
	/*< private >*/
	FU_APP_FLAGS_LAST
} FuAppFlags;

/**
 * FuDumpFlags:
 * @FU_DUMP_FLAGS_NONE:			No flags set
 * @FU_DUMP_FLAGS_SHOW_ASCII:		Show ASCII in debugging dumps
 * @FU_DUMP_FLAGS_SHOW_ADDRESSES:	Show addresses in debugging dumps
 *
 * The flags to use when configuring debugging
 **/
typedef enum {
	FU_DUMP_FLAGS_NONE		= 0,
	FU_DUMP_FLAGS_SHOW_ASCII	= 1 << 0,
	FU_DUMP_FLAGS_SHOW_ADDRESSES	= 1 << 1,
	/*< private >*/
	FU_DUMP_FLAGS_LAST
} FuDumpFlags;

typedef guint FuEndianType;

/**
 * FuPathKind:
 * @FU_PATH_KIND_CACHEDIR_PKG:		The cache directory (IE /var/cache/fwupd)
 * @FU_PATH_KIND_DATADIR_PKG:		The non-volatile data store (IE /usr/share/fwupd)
 * @FU_PATH_KIND_EFIAPPDIR:		The location to store EFI apps before install (IE /usr/libexec/fwupd/efi)
 * @FU_PATH_KIND_LOCALSTATEDIR:		The local state directory (IE /var)
 * @FU_PATH_KIND_LOCALSTATEDIR_PKG:	The local state directory for the package (IE /var/lib/fwupd)
 * @FU_PATH_KIND_PLUGINDIR_PKG:		The location to look for plugins for package (IE /usr/lib/[triplet]/fwupd-plugins-3)
 * @FU_PATH_KIND_SYSCONFDIR:		The configuration location (IE /etc)
 * @FU_PATH_KIND_SYSCONFDIR_PKG:	The package configuration location (IE /etc/fwupd)
 * @FU_PATH_KIND_SYSFSDIR_FW:		The sysfs firmware location (IE /sys/firmware)
 * @FU_PATH_KIND_SYSFSDIR_DRIVERS:	The platform sysfs directory (IE /sys/bus/platform/drivers)
 * @FU_PATH_KIND_SYSFSDIR_TPM:		The TPM sysfs directory (IE /sys/class/tpm)
 * @FU_PATH_KIND_POLKIT_ACTIONS:	The directory for policy kit actions (IE /usr/share/polkit-1/actions/)
 * @FU_PATH_KIND_OFFLINE_TRIGGER:	The file for the offline trigger (IE /system-update)
 * @FU_PATH_KIND_SYSFSDIR_SECURITY:	The sysfs security location (IE /sys/kernel/security)
 *
 * Path types to use when dynamically determining a path at runtime
 **/
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
	FU_PATH_KIND_POLKIT_ACTIONS,
	FU_PATH_KIND_OFFLINE_TRIGGER,
	FU_PATH_KIND_SYSFSDIR_SECURITY,
	/*< private >*/
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
gchar		*fu_common_realpath		(const gchar	*filename,
						 GError		**error);
gboolean	 fu_common_fnmatch		(const gchar	*pattern,
						 const gchar	*str);
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
gboolean	 fu_common_bytes_compare	(GBytes		*bytes1,
						 GBytes		*bytes2,
						 GError		**error);
gboolean	 fu_common_bytes_compare_raw	(const guint8	*buf1,
						 gsize		 bufsz1,
						 const guint8	*buf2,
						 gsize		 bufsz2,
						 GError		**error);
GBytes		*fu_common_bytes_pad		(GBytes		*bytes,
						 gsize		 sz);
gsize		 fu_common_strwidth		(const gchar	*text);
gboolean	 fu_memcpy_safe			(guint8		*dst,
						 gsize		 dst_sz,
						 gsize		 dst_offset,
						 const guint8	*src,
						 gsize		 src_sz,
						 gsize		 src_offset,
						 gsize		 n,
						 GError		**error);
gboolean	 fu_common_read_uint8_safe	(const guint8	*buf,
						 gsize		 bufsz,
						 gsize		 offset,
						 guint8		*value,
						 GError		**error);
gboolean	 fu_common_read_uint16_safe	(const guint8	*buf,
						 gsize		 bufsz,
						 gsize		 offset,
						 guint16	*value,
						 FuEndianType	 endian,
						 GError		**error);
gboolean	 fu_common_read_uint32_safe	(const guint8	*buf,
						 gsize		 bufsz,
						 gsize		 offset,
						 guint32	*value,
						 FuEndianType	 endian,
						 GError		**error);

void		 fu_byte_array_append_uint8	(GByteArray	*array,
						 guint8		 data);
void		 fu_byte_array_append_uint16	(GByteArray	*array,
						 guint16	 data,
						 FuEndianType	 endian);
void		 fu_byte_array_append_uint32	(GByteArray	*array,
						 guint32	 data,
						 FuEndianType	 endian);

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
void		 fu_common_string_append_kv	(GString	*str,
						 guint		 idt,
						 const gchar 	*key,
						 const gchar	*value);
void		 fu_common_string_append_ku	(GString	*str,
						 guint		 idt,
						 const gchar 	*key,
						 guint64	 value);
void		 fu_common_string_append_kx	(GString	*str,
						 guint		 idt,
						 const gchar 	*key,
						 guint64	 value);
void		 fu_common_string_append_kb	(GString	*str,
						 guint		 idt,
						 const gchar 	*key,
						 gboolean	 value);
gchar		**fu_common_strnsplit		(const gchar	*str,
						 gsize		 sz,
						 const gchar	*delimiter,
						 gint		 max_tokens);
gboolean	 fu_common_kernel_locked_down	(void);
