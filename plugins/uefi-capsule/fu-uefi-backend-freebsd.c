/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 3mdeb Embedded Systems Consulting
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-backend.h"

struct _FuUefiBackend {
	FuBackend		 parent_instance;
};

G_DEFINE_TYPE (FuUefiBackend, fu_uefi_backend, FU_TYPE_BACKEND)

static gchar *
fu_uefi_backend_get_sysctl_string (const gchar *name)
{
	gsize len = 0;
	g_autofree gchar *value = NULL;

	if (sysctlbyname (name, NULL, &len, NULL, 0))
		return NULL;
	value = g_malloc0 (len);
	if (sysctlbyname (name, value, &len, NULL, 0))
		return NULL;
	return g_steal_pointer (&value);
}

static guint64
fu_uefi_backend_get_sysctl_uint64 (const gchar *name)
{
	g_autofree gchar *value = fu_uefi_backend_get_sysctl_string (name);
	if (value == NULL)
		return G_MAXUINT64;
	return fu_common_strtoull (value);
}

/* yes, unsized uint_t */
static guint
fu_uefi_backend_get_entry_field (const gchar *sysctl_name, const gchar *field_name)
{
	g_autofree gchar *name = g_build_path (".", sysctl_name, field_name, NULL);
	return (guint) fu_uefi_backend_get_sysctl_uint64 (name);
}

static FuUefiDevice *
fu_uefi_backend_device_new (guint64 idx)
{
	g_autoptr(FuUefiDevice) dev = NULL;
	g_autofree gchar *fw_class = NULL;
	g_autofree gchar *fw_class_fn = NULL;
	g_autofree gchar *id = NULL;
	g_autofree gchar *sysctl_name = NULL;

	sysctl_name = g_strdup_printf ("hw.efi.esrt.entry%u", (guint) idx);
	fw_class_fn = g_build_path (".", sysctl_name, "fw_class", NULL);
	fw_class = fu_uefi_backend_get_sysctl_string (fw_class_fn);

	/* create object */
	dev = g_object_new (FU_TYPE_UEFI_DEVICE,
			    "fw-class", fw_class,
			    "capsule-flags", fu_uefi_backend_get_entry_field (sysctl_name, "capsule_flags"),
			    "kind", fu_uefi_backend_get_entry_field (sysctl_name, "fw_type"),
			    "fw-version", fu_uefi_backend_get_entry_field (sysctl_name, "fw_version"),
			    "last-attempt-status", fu_uefi_backend_get_entry_field (sysctl_name, "last_attempt_status"),
			    "last-attempt-version", fu_uefi_backend_get_entry_field (sysctl_name, "last_attempt_version"),
			    "fw-version-lowest", fu_uefi_backend_get_entry_field (sysctl_name, "lowest_supported_fw_version"),
			    "fmp-hardware-instance", (guint64) 0x0,
			    "version-format", FWUPD_VERSION_FORMAT_NUMBER,
			    NULL);

	/* set ID */
	id = g_strdup_printf ("UEFI-%s-dev0", fw_class);
	fu_device_set_id (FU_DEVICE (dev), id);
	fu_device_set_physical_id (FU_DEVICE (dev), sysctl_name);
	return g_steal_pointer (&dev);
}

static gboolean
fu_uefi_backend_setup (FuBackend *backend, GError **error)
{
	g_autofree gchar *efi_ver = fu_kenv_get_string ("efi-version", error);
	if (efi_ver == NULL) {
		g_prefix_error (error, "System does not support UEFI mode, no efi-version kenv: ");
		return FALSE;
	}
	if (fu_common_vercmp_full (efi_ver, "2.0.0.0", FWUPD_VERSION_FORMAT_QUAD) < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "System does not support UEFI mode, got efi-version of %s",
			     efi_ver);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_backend_coldplug (FuBackend *backend, GError **error)
{
	guint64 entry_count = 0;

	entry_count = fu_uefi_backend_get_sysctl_uint64 ("hw.efi.esrt.fw_resource_count");
	if (entry_count == G_MAXUINT64) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ESRT kernel support is missing");
		return FALSE;
	}
	for (guint i = 0; i < entry_count; i++) {
		g_autoptr(FuUefiDevice) dev = fu_uefi_backend_device_new (i);
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
	klass_backend->setup = fu_uefi_backend_setup;
	klass_backend->coldplug = fu_uefi_backend_coldplug;
}

FuBackend *
fu_uefi_backend_new (FuContext *ctx)
{
	return FU_BACKEND (g_object_new (FU_TYPE_UEFI_BACKEND,
					 "name", "uefi",
					 "context", ctx,
					 NULL));
}
