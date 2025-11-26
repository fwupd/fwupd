/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 3mdeb Embedded Systems Consulting
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <uuid.h>

#ifdef HAVE_FREEBSD_ESRT
#include <sys/efi.h>
#include <sys/efiio.h>
#endif

#include <glib/gstdio.h>

#include "fu-uefi-capsule-backend-freebsd.h"
#include "fu-uefi-capsule-device.h"
#include "fu-uefi-common.h"

struct _FuUefiCapsuleBackendFreebsd {
	FuUefiCapsuleBackend parent_instance;
};

G_DEFINE_TYPE(FuUefiCapsuleBackendFreebsd,
	      fu_uefi_capsule_backend_freebsd,
	      FU_TYPE_UEFI_CAPSULE_BACKEND)

#ifdef HAVE_FREEBSD_ESRT

static FuUefiCapsuleDevice *
fu_uefi_capsule_backend_device_new(FuUefiCapsuleBackend *self,
				   const gchar *physical_id,
				   struct efi_esrt_entry_v1 *entry,
				   guint64 idx,
				   GError **error)
{
	g_autoptr(FuUefiCapsuleDevice) dev = NULL;
	g_autofree gchar *backend_id = NULL;
	g_autofree gchar *fw_class = NULL;
	uint32_t status;

	uuid_to_string(&entry->fw_class, &fw_class, &status);
	if (status != uuid_s_ok) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "uuid_to_string error");
		return NULL;
	}

	/* create object */
	dev = g_object_new(fu_uefi_capsule_backend_get_device_gtype(self),
			   "fw-class",
			   fw_class,
			   "capsule-flags",
			   entry->capsule_flags,
			   "kind",
			   entry->fw_type,
			   "fw-version",
			   entry->fw_version,
			   "last-attempt-status",
			   entry->last_attempt_status,
			   "last-attempt-version",
			   entry->last_attempt_version,
			   "fw-version-lowest",
			   entry->lowest_supported_fw_version,
			   "fmp-hardware-instance",
			   (guint64)0x0,
			   "version-format",
			   FWUPD_VERSION_FORMAT_NUMBER,
			   NULL);

	/* set ID */
	backend_id = g_strdup_printf("ESRT/%u", (guint)idx);
	fu_device_set_backend_id(FU_DEVICE(dev), backend_id);
	fu_device_set_physical_id(FU_DEVICE(dev), physical_id);
	fu_device_set_logical_id(FU_DEVICE(dev), fw_class);
	return g_steal_pointer(&dev);
}

#endif

static gboolean
fu_uefi_capsule_backend_freebsd_setup(FuBackend *backend,
				    FuBackendSetupFlags flags,
				    FuProgress *progress,
				    GError **error)
{
	g_autofree gchar *efi_ver = fu_kenv_get_string("efi-version", error);
	if (efi_ver == NULL) {
		g_prefix_error_literal(error, "does not support UEFI, no efi-version kenv: ");
		return FALSE;
	}
	if (fu_version_compare(efi_ver, "2.0.0.0", FWUPD_VERSION_FORMAT_QUAD) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "does not support UEFI, got efi-version of %s",
			    efi_ver);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_capsule_backend_freebsd_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
#ifdef HAVE_FREEBSD_ESRT
	FuUefiCapsuleBackend *self = FU_UEFI_CAPSULE_BACKEND(backend);
	const gchar *esrt_dev = "/dev/efi";
	struct efi_get_table_ioc table = {.uuid = EFI_TABLE_ESRT};
	gint efi_fd;
	struct efi_esrt_entry_v1 *entries;
	g_autofree struct efi_esrt_table *esrt = NULL;

	efi_fd = g_open(esrt_dev, O_RDONLY, 0);
	if (efi_fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot open %s",
			    esrt_dev);
		return FALSE;
	}

	if (ioctl(efi_fd, EFIIOC_GET_TABLE, &table) == -1) {
		g_close(efi_fd, NULL);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Cannot determine size of ESRT table");
		return FALSE;
	}

	esrt = g_malloc(table.table_len);
	if (esrt == NULL) {
		g_close(efi_fd, NULL);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Cannot allocate memory for ESRT table");
		return FALSE;
	}

	table.buf = esrt;
	table.buf_len = table.table_len;
	if (ioctl(efi_fd, EFIIOC_GET_TABLE, &table) == -1) {
		g_close(efi_fd, NULL);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Cannot fill ESRT table");
		return FALSE;
	}

	entries = (struct efi_esrt_entry_v1 *)esrt->entries;
	for (guint i = 0; i < esrt->fw_resource_count; i++) {
		g_autoptr(FuUefiCapsuleDevice) dev = NULL;
		dev = fu_uefi_capsule_backend_device_new(self, esrt_dev, &entries[i], i, error);
		if (dev == NULL)
			return FALSE;

		fu_backend_device_added(backend, FU_DEVICE(dev));
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "ESRT access API is missing from the kernel");
	return FALSE;
#endif
}

static void
fu_uefi_capsule_backend_freebsd_set_device_gtype(FuBackend *backend, GType device_gtype)
{
}

static void
fu_uefi_capsule_backend_freebsd_init(FuUefiCapsuleBackendFreebsd *self)
{
}

static void
fu_uefi_capsule_backend_freebsd_class_init(FuUefiCapsuleBackendFreebsdClass *klass)
{
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	backend_class->setup = fu_uefi_capsule_backend_freebsd_setup;
	backend_class->coldplug = fu_uefi_capsule_backend_freebsd_coldplug;
}

FuBackend *
fu_uefi_capsule_backend_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_UEFI_CAPSULE_BACKEND_FREEBSD,
			    "name",
			    "uefi",
			    "context",
			    ctx,
			    NULL);
}
