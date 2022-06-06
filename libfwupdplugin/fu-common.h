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

gchar *
fu_common_get_path(FuPathKind path_kind);
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
GError *
fu_common_error_array_get_best(GPtrArray *errors);
gchar *
fu_common_find_program_in_path(const gchar *basename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
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
fu_common_reverse_uint8(guint8 value);

gchar *
fu_common_uri_get_scheme(const gchar *uri);
gsize
fu_common_align_up(gsize value, guint8 alignment);
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
