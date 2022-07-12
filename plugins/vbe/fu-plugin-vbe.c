/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vbe-simple-device.h"

struct FuPluginData {
	FuFirmware *fdt;
	gchar *vbe_dir;
};

static gboolean
fu_plugin_vbe_coldplug_img(FuPlugin *plugin,
			   FuFdtImage *fdt_root,
			   FuFdtImage *fdt_node,
			   GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	GType device_gtype = G_TYPE_INVALID;
	g_autofree gchar *compatible = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* we expect 'fwupd,vbe-<driver>' */
	if (!fu_fdt_image_get_attr_str(fdt_node,
				       FU_FIT_FIRMWARE_ATTR_COMPATIBLE,
				       &compatible,
				       error)) {
		g_prefix_error(error, "missing update mechanism: ");
		return FALSE;
	}
	split = g_strsplit(compatible, ",", 2);
	if (g_strv_length(split) != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "update mechanism is invalid: %s",
			    compatible);
		return FALSE;
	}
	if (g_strcmp0(split[0], "fwupd") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "update mechanism should have manufacturer of fwupd: %s",
			    split[0]);
		return FALSE;
	}

	/* skip past 'vbe-' */
	if (!g_str_has_prefix(split[1], "vbe-")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "update mechanism is missing vbe prefix: %s",
			    split[1]);
		return FALSE;
	}
	if (g_strcmp0(split[1], "vbe-simple") == 0) {
		device_gtype = FU_TYPE_VBE_SIMPLE_DEVICE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no driver for VBE method '%s'",
			    split[1]);
		return FALSE;
	}

	/* success */
	dev = g_object_new(device_gtype,
			   "context",
			   fu_plugin_get_context(plugin),
			   "fdt-root",
			   fdt_root,
			   "fdt-node",
			   fdt_node,
			   "vbe-dir",
			   priv->vbe_dir,
			   NULL);
	fu_plugin_device_add(plugin, dev);
	return TRUE;
}

static gboolean
fu_plugin_vbe_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(FuFdtImage) fdt_root = NULL;
	g_autoptr(GPtrArray) fdt_imgs = NULL;

	/* get compatible from root node */
	fdt_root =
	    fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(priv->fdt), "/chosen/fwupd", error);
	if (fdt_root == NULL)
		return FALSE;
	fdt_imgs = fu_firmware_get_images(FU_FIRMWARE(fdt_root));
	for (guint i = 0; i < fdt_imgs->len; i++) {
		FuFdtImage *fdt_node = g_ptr_array_index(fdt_imgs, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_plugin_vbe_coldplug_img(plugin, fdt_root, fdt_node, &error_local)) {
			g_warning("%s", error_local->message);
			continue;
		}
	}

	/* nothing found? */
	if (fu_plugin_get_devices(plugin)->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no valid VBE update mechanism found");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GFile *
fu_plugin_vbe_get_bfname(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autofree gchar *bfname_local = NULL;
	g_autofree gchar *bfname_sys = NULL;
	g_autofree gchar *sysfsdir = NULL;

	/* look for override first, fall back to system value */
	bfname_local = g_build_filename(priv->vbe_dir, "system.dtb", NULL);
	if (g_file_test(bfname_local, G_FILE_TEST_EXISTS))
		return g_file_new_for_path(bfname_local);

	/* actual hardware value */
	sysfsdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	bfname_sys = g_build_filename(sysfsdir, "fdt", NULL);
	return g_file_new_for_path(bfname_sys);
}

static gboolean
fu_plugin_vbe_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autoptr(GFile) file = NULL;

	/* look for override first, fall back to system value */
	file = fu_plugin_vbe_get_bfname(plugin);
	if (!fu_firmware_parse_file(priv->fdt, file, FWUPD_INSTALL_FLAG_NO_SEARCH, error)) {
		g_prefix_error(error, "failed to parse FDT: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_vbe_init(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
	g_autofree gchar *localstatedir_pkg = NULL;

	/* where we can store the override and also image state */
	localstatedir_pkg = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	priv->vbe_dir = g_build_filename(localstatedir_pkg, "vbe", NULL);
	priv->fdt = fu_fdt_firmware_new();
}

static void
fu_plugin_vbe_destroy(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_free(priv->vbe_dir);
	g_object_unref(priv->fdt);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_vbe_init;
	vfuncs->destroy = fu_plugin_vbe_destroy;
	vfuncs->startup = fu_plugin_vbe_startup;
	vfuncs->coldplug = fu_plugin_vbe_coldplug;
}
