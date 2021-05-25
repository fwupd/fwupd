/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-backend.h"

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

static void
fu_uefi_backend_init (FuUefiBackend *self)
{
}

static void
fu_uefi_backend_class_init (FuUefiBackendClass *klass)
{
	FuBackendClass *klass_backend = FU_BACKEND_CLASS (klass);
	klass_backend->coldplug = fu_uefi_backend_coldplug;
}

FuBackend *
fu_uefi_backend_new (void)
{
	return FU_BACKEND (g_object_new (FU_TYPE_UEFI_BACKEND, "name", "uefi", NULL));
}
