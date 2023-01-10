/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include <fwupdplugin.h>

#include "fu-fdt-backend.h"
#include "fu-fdt-device.h"

struct _FuFdtBackend {
	FuBackend parent_instance;
};

G_DEFINE_TYPE(FuFdtBackend, fu_fdt_backend, FU_TYPE_BACKEND)

static GFile *
fu_fdt_backend_get_bfname(FuFdtBackend *self, GError **error)
{
	g_autofree gchar *bfname_local = NULL;
	g_autofree gchar *bfname_sys = NULL;
	g_autofree gchar *localstatedir_pkg = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	g_autofree gchar *sysfsdir = NULL;

	/* look for override first, fall back to system value */
	bfname_local = g_build_filename(localstatedir_pkg, "vbe", "system.dtb", NULL);
	if (g_file_test(bfname_local, G_FILE_TEST_EXISTS))
		return g_file_new_for_path(bfname_local);

	/* actual hardware value */
	sysfsdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	bfname_sys = g_build_filename(sysfsdir, "fdt", NULL);
	if (g_file_test(bfname_sys, G_FILE_TEST_EXISTS))
		return g_file_new_for_path(bfname_sys);

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot find %s or override %s",
		    bfname_sys,
		    bfname_local);
	return NULL;
}

static gboolean
fu_fdt_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuFdtBackend *self = FU_FDT_BACKEND(backend);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) fdt_firmware = fu_fdt_firmware_new();
	g_autoptr(GFile) file = NULL;

	/* look for override first, fall back to system value */
	file = fu_fdt_backend_get_bfname(self, error);
	if (file == NULL)
		return FALSE;
	if (!fu_firmware_parse_file(fdt_firmware, file, FWUPD_INSTALL_FLAG_NO_SEARCH, error)) {
		g_prefix_error(error, "failed to parse FDT: ");
		return FALSE;
	}

	/* add device */
	device = g_object_new(FU_TYPE_FDT_DEVICE, "fdt-firmware", fdt_firmware, NULL);
	fu_backend_device_added(backend, device);
	return TRUE;
}

static void
fu_fdt_backend_init(FuFdtBackend *self)
{
}

static void
fu_fdt_backend_class_init(FuFdtBackendClass *klass)
{
	FuBackendClass *klass_backend = FU_BACKEND_CLASS(klass);
	klass_backend->coldplug = fu_fdt_backend_coldplug;
}

FuBackend *
fu_fdt_backend_new(FuContext *ctx)
{
	FuFdtBackend *self;
	self =
	    FU_FDT_BACKEND(g_object_new(FU_TYPE_FDT_BACKEND, "name", "fdt", "context", ctx, NULL));
	return FU_BACKEND(self);
}
