/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gunixmounts.h>

#include "fu-common.h"

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-backend.h"

#ifndef HAVE_GIO_2_55_0
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUnixMountEntry, g_unix_mount_free)
#pragma clang diagnostic pop
#endif

struct _FuUefiBackend {
	FuBackend		 parent_instance;
};

G_DEFINE_TYPE (FuUefiBackend, fu_uefi_backend, FU_TYPE_BACKEND)

/* yes, unsized uint_t */
static guint
fu_uefi_backend_read (const gchar *path, const gchar *filename)
{
	return fu_uefi_read_file_as_uint64 (path, filename);
}

static FuUefiDevice *
fu_uefi_backend_device_new (const gchar *path)
{
	g_autoptr(FuUefiDevice) dev = NULL;
	g_autofree gchar *fw_class = NULL;
	g_autofree gchar *fw_class_fn = NULL;
	g_autofree gchar *id = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	/* read values from sysfs */
	fw_class_fn = g_build_filename (path, "fw_class", NULL);
	if (g_file_get_contents (fw_class_fn, &fw_class, NULL, NULL))
		g_strdelimit (fw_class, "\n", '\0');

	/* Create object, assuming a verfmt of NUMBER unless told otherwise by
	 * a quirk entry or metadata.
	 *
	 * The hardware instance is not in the ESRT table and we should really
	 * write the EFI stub to query with FMP -- but we still have not ever
	 * seen a PCIe device with FMP support... */
	dev = g_object_new (FU_TYPE_UEFI_DEVICE,
			    "fw-class", fw_class,
			    "capsule-flags", fu_uefi_backend_read (path, "capsule_flags"),
			    "kind", fu_uefi_backend_read (path, "fw_type"),
			    "fw-version", fu_uefi_backend_read (path, "fw_version"),
			    "last-attempt-status", fu_uefi_backend_read (path, "last_attempt_status"),
			    "last-attempt-version", fu_uefi_backend_read (path, "last_attempt_version"),
			    "fw-version-lowest", fu_uefi_backend_read (path, "lowest_supported_fw_version"),
			    "fmp-hardware-instance", (guint64) 0x0,
			    "version-format", FWUPD_VERSION_FORMAT_NUMBER,
			    NULL);

	/* set ID */
	id = g_strdup_printf ("UEFI-%s-dev0", fw_class);
	fu_device_set_id (FU_DEVICE (dev), id);
	fu_device_set_physical_id (FU_DEVICE (dev), path);
	return g_steal_pointer (&dev);
}

static gboolean
fu_uefi_backend_coldplug (FuBackend *backend, GError **error)
{
	const gchar *fn;
	g_autofree gchar *esrt_entries = NULL;
	g_autofree gchar *esrt_path = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get the directory of ESRT entries */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	esrt_path = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);
	esrt_entries = g_build_filename (esrt_path, "entries", NULL);
	dir = g_dir_open (esrt_entries, 0, error);
	if (dir == NULL)
		return FALSE;

	/* add each device */
	while ((fn = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *path = g_build_filename (esrt_entries, fn, NULL);
		g_autoptr(FuUefiDevice) dev = fu_uefi_backend_device_new (path);
		fu_backend_device_added (backend, FU_DEVICE (dev));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_backend_check_smbios_enabled (FuContext *ctx, GError **error)
{
	const guint8 *data;
	gsize sz;
	g_autoptr(GBytes) bios_information = fu_context_get_smbios_data (ctx, 0);
	if (bios_information == NULL) {
		const gchar *tmp = g_getenv ("FWUPD_DELL_FAKE_SMBIOS");
		if (tmp != NULL)
			return TRUE;
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "SMBIOS not supported");
		return FALSE;
	}
	data = g_bytes_get_data (bios_information, &sz);
	if (sz < 0x14) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "offset bigger than size %" G_GSIZE_FORMAT, sz);
		return FALSE;
	}
	if (data[1] < 0x14) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "SMBIOS 2.3 not supported");
		return FALSE;
	}
	if (!(data[0x13] & (1 << 3))) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "System does not support UEFI mode");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_backend_check_efivarfs_rw (GError **error)
{
	g_autofree gchar *sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	g_autofree gchar *sysfsefivardir = g_build_filename (sysfsfwdir, "efi", "efivars", NULL);
	g_autoptr(GUnixMountEntry) mount = g_unix_mount_at (sysfsefivardir, NULL);

	if (mount == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "%s was not mounted", sysfsefivardir);
		return FALSE;
	}
	if (g_unix_mount_is_readonly (mount)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "%s is read only", sysfsefivardir);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_uefi_backend_setup (FuBackend *backend, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_efivarfs = NULL;

	/* using a pre-cooked SMBIOS */
	if (g_getenv ("FWUPD_SYSFSFWDIR") != NULL)
		return TRUE;

	/* make sure that efivarfs is rw */
	if (!fu_uefi_backend_check_efivarfs_rw (error))
		return FALSE;

	/* check SMBIOS for 'UEFI Specification is supported' */
	if (!fu_uefi_backend_check_smbios_enabled (fu_backend_get_context (backend), &error_local)) {
		g_autofree gchar *fw = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
		g_autofree gchar *fn = g_build_filename (fw, "efi", NULL);
		if (g_file_test (fn, G_FILE_TEST_EXISTS)) {
			g_warning ("SMBIOS BIOS Characteristics Extension Byte 2 is invalid -- "
				   "UEFI Specification is unsupported, but %s exists: %s",
				   fn, error_local->message);
			return TRUE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	return TRUE;
}

static void
fu_uefi_backend_init (FuUefiBackend *self)
{
}

static void
fu_uefi_backend_class_init (FuUefiBackendClass *klass)
{
	FuBackendClass *klass_backend = FU_BACKEND_CLASS (klass);
	klass_backend->coldplug = fu_uefi_backend_coldplug;
	klass_backend->setup = fu_uefi_backend_setup;
}

FuBackend *
fu_uefi_backend_new (FuContext *ctx)
{
	return FU_BACKEND (g_object_new (FU_TYPE_UEFI_BACKEND,
					 "name", "uefi",
					 "context", ctx,
					 NULL));
}
