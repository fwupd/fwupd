/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#include <xmlb.h>

#include "fu-volume.h"

/**
 * FuAppFlags:
 * @FU_APP_FLAGS_NONE:			No flags set
 * @FU_APP_FLAGS_NO_IDLE_SOURCES:	Disallow idle sources
 *
 * The flags to use when loading an application.
 **/
typedef enum {
	FU_APP_FLAGS_NONE = 0,
	FU_APP_FLAGS_NO_IDLE_SOURCES = 1 << 0,
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
	FU_DUMP_FLAGS_NONE = 0,
	FU_DUMP_FLAGS_SHOW_ASCII = 1 << 0,
	FU_DUMP_FLAGS_SHOW_ADDRESSES = 1 << 1,
	/*< private >*/
	FU_DUMP_FLAGS_LAST
} FuDumpFlags;

/**
 * FuEndianType:
 *
 * The endian type, e.g. %G_LITTLE_ENDIAN
 **/
typedef guint FuEndianType;

/**
 * FuPathKind:
 * @FU_PATH_KIND_CACHEDIR_PKG:		The cache directory (IE /var/cache/fwupd)
 * @FU_PATH_KIND_DATADIR_PKG:		The non-volatile data store (IE /usr/share/fwupd)
 * @FU_PATH_KIND_EFIAPPDIR:		The location to store EFI apps before install (IE
 * /usr/libexec/fwupd/efi)
 * @FU_PATH_KIND_LOCALSTATEDIR:		The local state directory (IE /var)
 * @FU_PATH_KIND_LOCALSTATEDIR_PKG:	The local state directory for the package (IE
 * /var/lib/fwupd)
 * @FU_PATH_KIND_PLUGINDIR_PKG:		The location to look for plugins for package (IE
 * /usr/lib/[triplet]/fwupd-plugins-3)
 * @FU_PATH_KIND_SYSCONFDIR:		The configuration location (IE /etc)
 * @FU_PATH_KIND_SYSCONFDIR_PKG:	The package configuration location (IE /etc/fwupd)
 * @FU_PATH_KIND_SYSFSDIR_FW:		The sysfs firmware location (IE /sys/firmware)
 * @FU_PATH_KIND_SYSFSDIR_DRIVERS:	The platform sysfs directory (IE /sys/bus/platform/drivers)
 * @FU_PATH_KIND_SYSFSDIR_TPM:		The TPM sysfs directory (IE /sys/class/tpm)
 * @FU_PATH_KIND_PROCFS:		The procfs location (IE /proc)
 * @FU_PATH_KIND_POLKIT_ACTIONS:	The directory for policy kit actions (IE
 * /usr/share/polkit-1/actions/)
 * @FU_PATH_KIND_OFFLINE_TRIGGER:	The file for the offline trigger (IE /system-update)
 * @FU_PATH_KIND_SYSFSDIR_SECURITY:	The sysfs security location (IE /sys/kernel/security)
 * @FU_PATH_KIND_ACPI_TABLES:		The location of the ACPI tables
 * @FU_PATH_KIND_LOCKDIR:		The lock directory (IE /run/lock)
 * @FU_PATH_KIND_SYSFSDIR_FW_ATTRIB	The firmware attributes directory (IE
 * /sys/class/firmware-attributes)
 * @FU_PATH_KIND_FIRMWARE_SEARCH:	The path to configure the kernel policy for runtime loading
 *other than /lib/firmware (IE /sys/module/firmware_class/parameters/path)
 * @FU_PATH_KIND_DATADIR_QUIRKS:	The quirks data store (IE /usr/share/fwupd/quirks.d)
 * @FU_PATH_KIND_LOCALSTATEDIR_QUIRKS:	The local state directory for quirks (IE
 * /var/lib/fwupd/quirks.d)
 * @FU_PATH_KIND_LOCALSTATEDIR_METADATA: The local state directory for metadata (IE
 * /var/lib/fwupd/metadata)
 * @FU_PATH_KIND_LOCALSTATEDIR_REMOTES: The local state directory for remotes (IE
 * /var/lib/fwupd/remotes.d)
 * @FU_PATH_KIND_WIN32_BASEDIR:		The root of the install directory on Windows
 * @FU_PATH_KIND_LOCALCONFDIR_PKG:	The package configuration override (IE /var/etc/fwupd)
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
	FU_PATH_KIND_PROCFS,
	FU_PATH_KIND_POLKIT_ACTIONS,
	FU_PATH_KIND_OFFLINE_TRIGGER,
	FU_PATH_KIND_SYSFSDIR_SECURITY,
	FU_PATH_KIND_ACPI_TABLES,
	FU_PATH_KIND_LOCKDIR,
	FU_PATH_KIND_SYSFSDIR_FW_ATTRIB,
	FU_PATH_KIND_FIRMWARE_SEARCH,
	FU_PATH_KIND_DATADIR_QUIRKS,
	FU_PATH_KIND_LOCALSTATEDIR_QUIRKS,
	FU_PATH_KIND_LOCALSTATEDIR_METADATA,
	FU_PATH_KIND_LOCALSTATEDIR_REMOTES,
	FU_PATH_KIND_WIN32_BASEDIR,
	FU_PATH_KIND_LOCALCONFDIR_PKG,
	/*< private >*/
	FU_PATH_KIND_LAST
} FuPathKind;

/**
 * FuCpuVendor:
 * @FU_CPU_VENDOR_UNKNOWN:		Unknown
 * @FU_CPU_VENDOR_INTEL:		Intel
 * @FU_CPU_VENDOR_AMD:			AMD
 *
 * The CPU vendor.
 **/
typedef enum {
	FU_CPU_VENDOR_UNKNOWN,
	FU_CPU_VENDOR_INTEL,
	FU_CPU_VENDOR_AMD,
	/*< private >*/
	FU_CPU_VENDOR_LAST
} FuCpuVendor;

/**
 * FU_BATTERY_VALUE_INVALID:
 *
 * This value signifies the battery level is either unset, or the value cannot
 * be discovered.
 */
#define FU_BATTERY_VALUE_INVALID 101

/**
 * FuBatteryState:
 * @FU_BATTERY_STATE_UNKNOWN:		Unknown
 * @FU_BATTERY_STATE_CHARGING:		Charging
 * @FU_BATTERY_STATE_DISCHARGING:	Discharging
 * @FU_BATTERY_STATE_EMPTY:		Empty
 * @FU_BATTERY_STATE_FULLY_CHARGED:	Fully charged
 *
 * The device battery state.
 **/
typedef enum {
	FU_BATTERY_STATE_UNKNOWN,
	FU_BATTERY_STATE_CHARGING,
	FU_BATTERY_STATE_DISCHARGING,
	FU_BATTERY_STATE_EMPTY,
	FU_BATTERY_STATE_FULLY_CHARGED,
	/*< private >*/
	FU_BATTERY_STATE_LAST
} FuBatteryState;

/**
 * FuLidState:
 * @FU_LID_STATE_UNKNOWN:		Unknown
 * @FU_LID_STATE_OPEN:			Charging
 * @FU_LID_STATE_CLOSED:		Discharging
 *
 * The device lid state.
 **/
typedef enum {
	FU_LID_STATE_UNKNOWN,
	FU_LID_STATE_OPEN,
	FU_LID_STATE_CLOSED,
	/*< private >*/
	FU_LID_STATE_LAST
} FuLidState;

/**
 * FuOutputHandler:
 * @line: text data
 * @user_data: user data
 *
 * The process spawn iteration callback.
 */
typedef void (*FuOutputHandler)(const gchar *line, gpointer user_data);

gboolean
fu_common_spawn_sync(const gchar *const *argv,
		     FuOutputHandler handler_cb,
		     gpointer handler_user_data,
		     guint timeout_ms,
		     GCancellable *cancellable,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT;

gchar *
fu_common_get_path(FuPathKind path_kind);
gchar *
fu_common_realpath(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fu_common_filename_glob(const gchar *directory,
			const gchar *pattern,
			GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_fnmatch(const gchar *pattern, const gchar *str);
gboolean
fu_common_rmtree(const gchar *directory, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fu_common_get_files_recursive(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_mkdir(const gchar *dirname, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_mkdir_parent(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_set_contents_bytes(const gchar *filename,
			     GBytes *bytes,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_common_get_contents_bytes(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_common_get_contents_stream(GInputStream *stream,
			      gsize count,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_common_get_contents_fd(gint fd, gsize count, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_extract_archive(GBytes *blob, const gchar *dir, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_common_firmware_builder(GBytes *bytes,
			   const gchar *script_fn,
			   const gchar *output_fn,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
GError *
fu_common_error_array_get_best(GPtrArray *errors);
guint64
fu_common_strtoull(const gchar *str);
gboolean
fu_common_strtoull_full(const gchar *str, guint64 *value, guint64 min, guint64 max, GError **error);
gchar *
fu_common_find_program_in_path(const gchar *basename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gchar *
fu_common_strstrip(const gchar *str);
void
fu_common_dump_raw(const gchar *log_domain, const gchar *title, const guint8 *data, gsize len);
void
fu_common_dump_full(const gchar *log_domain,
		    const gchar *title,
		    const guint8 *data,
		    gsize len,
		    guint columns,
		    FuDumpFlags flags);
void
fu_common_dump_bytes(const gchar *log_domain, const gchar *title, GBytes *bytes);
GBytes *
fu_common_bytes_align(GBytes *bytes, gsize blksz, gchar padval);
const guint8 *
fu_bytes_get_data_safe(GBytes *bytes, gsize *bufsz, GError **error);
gboolean
fu_common_bytes_is_empty(GBytes *bytes);
gboolean
fu_common_bytes_compare(GBytes *bytes1, GBytes *bytes2, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_bytes_compare_raw(const guint8 *buf1,
			    gsize bufsz1,
			    const guint8 *buf2,
			    gsize bufsz2,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_common_bytes_pad(GBytes *bytes, gsize sz);
GBytes *
fu_common_bytes_new_offset(GBytes *bytes, gsize offset, gsize length, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gsize
fu_common_strwidth(const gchar *text);
guint8 *
fu_memdup_safe(const guint8 *src, gsize n, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memcpy_safe(guint8 *dst,
	       gsize dst_sz,
	       gsize dst_offset,
	       const guint8 *src,
	       gsize src_sz,
	       gsize src_offset,
	       gsize n,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memmem_safe(const guint8 *haystack,
	       gsize haystack_sz,
	       const guint8 *needle,
	       gsize needle_sz,
	       gsize *offset,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_read_uint8_safe(const guint8 *buf,
			  gsize bufsz,
			  gsize offset,
			  guint8 *value,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_read_uint16_safe(const guint8 *buf,
			   gsize bufsz,
			   gsize offset,
			   guint16 *value,
			   FuEndianType endian,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_read_uint32_safe(const guint8 *buf,
			   gsize bufsz,
			   gsize offset,
			   guint32 *value,
			   FuEndianType endian,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_read_uint64_safe(const guint8 *buf,
			   gsize bufsz,
			   gsize offset,
			   guint64 *value,
			   FuEndianType endian,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_write_uint8_safe(guint8 *buf, gsize bufsz, gsize offset, guint8 value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_write_uint16_safe(guint8 *buf,
			    gsize bufsz,
			    gsize offset,
			    guint16 value,
			    FuEndianType endian,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_write_uint32_safe(guint8 *buf,
			    gsize bufsz,
			    gsize offset,
			    guint32 value,
			    FuEndianType endian,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_write_uint64_safe(guint8 *buf,
			    gsize bufsz,
			    gsize offset,
			    guint64 value,
			    FuEndianType endian,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;

void
fu_byte_array_set_size(GByteArray *array, guint length);
void
fu_byte_array_set_size_full(GByteArray *array, guint length, guint8 data);
void
fu_byte_array_align_up(GByteArray *array, guint8 alignment, guint8 data);
void
fu_byte_array_append_uint8(GByteArray *array, guint8 data);
void
fu_byte_array_append_uint16(GByteArray *array, guint16 data, FuEndianType endian);
void
fu_byte_array_append_uint32(GByteArray *array, guint32 data, FuEndianType endian);
void
fu_byte_array_append_uint64(GByteArray *array, guint64 data, FuEndianType endian);
void
fu_byte_array_append_bytes(GByteArray *array, GBytes *bytes);
gboolean
fu_byte_array_compare(GByteArray *buf1, GByteArray *buf2, GError **error);

void
fu_common_write_uint16(guint8 *buf, guint16 val_native, FuEndianType endian);
void
fu_common_write_uint24(guint8 *buf, guint32 val_native, FuEndianType endian);
void
fu_common_write_uint32(guint8 *buf, guint32 val_native, FuEndianType endian);
void
fu_common_write_uint64(guint8 *buf, guint64 val_native, FuEndianType endian);
guint16
fu_common_read_uint16(const guint8 *buf, FuEndianType endian);
guint32
fu_common_read_uint24(const guint8 *buf, FuEndianType endian);
guint32
fu_common_read_uint32(const guint8 *buf, FuEndianType endian);
guint64
fu_common_read_uint64(const guint8 *buf, FuEndianType endian);

guint
fu_common_string_replace(GString *string, const gchar *search, const gchar *replace);
void
fu_common_string_append_kv(GString *str, guint idt, const gchar *key, const gchar *value);
void
fu_common_string_append_ku(GString *str, guint idt, const gchar *key, guint64 value);
void
fu_common_string_append_kx(GString *str, guint idt, const gchar *key, guint64 value);
void
fu_common_string_append_kb(GString *str, guint idt, const gchar *key, gboolean value);
gchar **
fu_common_strnsplit(const gchar *str, gsize sz, const gchar *delimiter, gint max_tokens);

/**
 * FuCommonStrsplitFunc:
 * @token: a #GString
 * @token_idx: the token number
 * @user_data: user data
 * @error: a #GError or NULL
 *
 * The fu_common_strnsplit_full() iteration callback.
 */
typedef gboolean (*FuCommonStrsplitFunc)(GString *token,
					 guint token_idx,
					 gpointer user_data,
					 GError **error);
gboolean
fu_common_strnsplit_full(const gchar *str,
			 gssize sz,
			 const gchar *delimiter,
			 FuCommonStrsplitFunc callback,
			 gpointer user_data,
			 GError **error);

gchar *
fu_common_strsafe(const gchar *str, gsize maxsz);
gchar *
fu_common_instance_id_strsafe(const gchar *str);
gchar *
fu_common_strjoin_array(const gchar *separator, GPtrArray *array);
gboolean
fu_common_kernel_locked_down(void);
gboolean
fu_common_check_kernel_version(const gchar *minimum_kernel, GError **error);
gboolean
fu_common_cpuid(guint32 leaf,
		guint32 *eax,
		guint32 *ebx,
		guint32 *ecx,
		guint32 *edx,
		GError **error) G_GNUC_WARN_UNUSED_RESULT;
FuCpuVendor
fu_common_get_cpu_vendor(void);
gboolean
fu_common_is_live_media(void);
guint64
fu_common_get_memory_size(void);
GPtrArray *
fu_common_get_volumes_by_kind(const gchar *kind, GError **error) G_GNUC_WARN_UNUSED_RESULT;
FuVolume *
fu_common_get_volume_by_device(const gchar *device, GError **error) G_GNUC_WARN_UNUSED_RESULT;
FuVolume *
fu_common_get_volume_by_devnum(guint32 devnum, GError **error) G_GNUC_WARN_UNUSED_RESULT;
FuVolume *
fu_common_get_esp_for_path(const gchar *esp_path, GError **error) G_GNUC_WARN_UNUSED_RESULT;
FuVolume *
fu_common_get_esp_default(GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_common_check_full_disk_encryption(GError **error);

guint8
fu_common_crc8(const guint8 *buf, gsize bufsz);
guint8
fu_common_crc8_full(const guint8 *buf, gsize bufsz, guint8 crc_init, guint8 polynomial);
guint16
fu_common_crc16(const guint8 *buf, gsize bufsz);
guint16
fu_common_crc16_full(const guint8 *buf, gsize bufsz, guint16 crc, guint16 polynomial);
guint32
fu_common_crc32(const guint8 *buf, gsize bufsz);
guint32
fu_common_crc32_full(const guint8 *buf, gsize bufsz, guint32 crc, guint32 polynomial);

guint8
fu_common_reverse_uint8(guint8 value);

guint8
fu_common_sum8(const guint8 *buf, gsize bufsz);
guint8
fu_common_sum8_bytes(GBytes *blob);
guint16
fu_common_sum16(const guint8 *buf, gsize bufsz);
guint16
fu_common_sum16_bytes(GBytes *blob);
guint16
fu_common_sum16w(const guint8 *buf, gsize bufsz, FuEndianType endian);
guint16
fu_common_sum16w_bytes(GBytes *blob, FuEndianType endian);
guint32
fu_common_sum32(const guint8 *buf, gsize bufsz);
guint32
fu_common_sum32_bytes(GBytes *blob);
guint32
fu_common_sum32w(const guint8 *buf, gsize bufsz, FuEndianType endian);
guint32
fu_common_sum32w_bytes(GBytes *blob, FuEndianType endian);

gchar *
fu_common_uri_get_scheme(const gchar *uri);
gsize
fu_common_align_up(gsize value, guint8 alignment);
gchar *
fu_common_get_firmware_search_path(GError **error);
gboolean
fu_common_set_firmware_search_path(const gchar *path, GError **error);
gboolean
fu_common_reset_firmware_search_path(GError **error);
const gchar *
fu_battery_state_to_string(FuBatteryState battery_state);
const gchar *
fu_lid_state_to_string(FuLidState lid_state);

void
fu_xmlb_builder_insert_kv(XbBuilderNode *bn, const gchar *key, const gchar *value);
void
fu_xmlb_builder_insert_kx(XbBuilderNode *bn, const gchar *key, guint64 value);
void
fu_xmlb_builder_insert_kb(XbBuilderNode *bn, const gchar *key, gboolean value);
