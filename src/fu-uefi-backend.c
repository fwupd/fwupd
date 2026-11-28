/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include "fu-device-private.h"
#include "fu-efivars-private.h"
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
	FuEfiSecureBootSiglistKind kinds[] = {
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK,
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK,
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
	};

	/* each important EFI key */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, G_N_ELEMENTS(kinds));
	for (guint i = 0; i < G_N_ELEMENTS(kinds); i++) {
		g_autoptr(FuUefiDevice) uefi_device = NULL;
		if (!fu_efivars_exists(efivars,
				       fu_efivars_secure_boot_siglist_kind_to_guid(kinds[i]),
				       fu_efivars_secure_boot_siglist_kind_to_name(kinds[i]))) {
			fu_progress_step_done(progress);
			continue;
		}
		uefi_device = fu_uefi_device_new(ctx, kinds[i]);
		fu_backend_device_added(backend, FU_DEVICE(uefi_device));
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
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
