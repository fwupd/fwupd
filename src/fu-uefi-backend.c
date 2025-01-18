/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include <fwupdplugin.h>

#include "fu-uefi-backend.h"
#include "fu-uefi-device-private.h"

struct _FuUefiBackend {
	FuBackend parent_instance;
};

G_DEFINE_TYPE(FuUefiBackend, fu_uefi_backend, FU_TYPE_BACKEND)

static gboolean
fu_uefi_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_backend_get_context(backend);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	struct {
		const gchar *guid;
		const gchar *name;
	} guid_names[] = {
	    {FU_EFIVARS_GUID_EFI_GLOBAL, "PK"},
	    {FU_EFIVARS_GUID_EFI_GLOBAL, "KEK"},
	    {FU_EFIVARS_GUID_SECURITY_DATABASE, "db"},
	    {FU_EFIVARS_GUID_SECURITY_DATABASE, "dbx"},
	};

	/* each important EFI key */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, G_N_ELEMENTS(guid_names));
	for (guint i = 0; i < G_N_ELEMENTS(guid_names); i++) {
		g_autoptr(FuUefiDevice) uefi_device = NULL;
		if (!fu_efivars_exists(efivars, guid_names[i].guid, guid_names[i].name)) {
			fu_progress_step_done(progress);
			continue;
		}
		uefi_device = fu_uefi_device_new(guid_names[i].guid, guid_names[i].name);
		fu_backend_device_added(backend, FU_DEVICE(uefi_device));
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static FuDevice *
fu_uefi_backend_create_device(FuBackend *backend, const gchar *backend_id, GError **error)
{
	g_auto(GStrv) split = g_strsplit(backend_id, "-", 2);
	g_autoptr(FuUefiDevice) uefi_device = NULL;

	uefi_device =
	    g_object_new(FU_TYPE_UEFI_DEVICE, "backend", backend, "backend-id", backend_id, NULL);
	fu_uefi_device_set_guid(uefi_device, split[0]);
	fu_uefi_device_set_name(uefi_device, split[1]);
	return FU_DEVICE(g_steal_pointer(&uefi_device));
}

static void
fu_uefi_backend_init(FuUefiBackend *self)
{
}

static void
fu_uefi_backend_class_init(FuUefiBackendClass *klass)
{
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	backend_class->coldplug = fu_uefi_backend_coldplug;
	backend_class->create_device = fu_uefi_backend_create_device;
}

FuBackend *
fu_uefi_backend_new(FuContext *ctx)
{
	return FU_BACKEND(g_object_new(FU_TYPE_UEFI_BACKEND,
				       "name",
				       "uefi",
				       "context",
				       ctx,
				       "device-gtype",
				       FU_TYPE_UEFI_DEVICE,
				       NULL));
}
