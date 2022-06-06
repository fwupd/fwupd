/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

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

gchar *
fu_path_from_kind(FuPathKind path_kind);
GPtrArray *
fu_path_glob(const gchar *directory,
	     const gchar *pattern,
	     GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_path_fnmatch(const gchar *pattern, const gchar *str);
gboolean
fu_path_rmtree(const gchar *directory, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fu_path_get_files(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_path_mkdir(const gchar *dirname, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_path_mkdir_parent(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gchar *
fu_path_find_program(const gchar *basename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
