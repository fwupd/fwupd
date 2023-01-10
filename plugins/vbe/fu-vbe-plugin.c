/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vbe-plugin.h"
#include "fu-vbe-simple-device.h"

struct _FuVbePlugin {
	FuPlugin parent_instance;
	FuFirmware *fdt;
	gchar *vbe_dir;
};

G_DEFINE_TYPE(FuVbePlugin, fu_vbe_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_vbe_plugin_coldplug_img(FuPlugin *plugin,
			   FuFdtImage *fdt_root,
			   FuFdtImage *fdt_node,
			   GError **error)
{
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
			   NULL);
	fu_plugin_device_add(plugin, dev);
	return TRUE;
}

static gboolean
fu_vbe_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuVbePlugin *self = FU_VBE_PLUGIN(plugin);
	g_autoptr(FuFdtImage) fdt_root = NULL;
	g_autoptr(GPtrArray) fdt_imgs = NULL;

	/* get compatible from root node */
	fdt_root =
	    fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(self->fdt), "/chosen/fwupd", error);
	if (fdt_root == NULL)
		return FALSE;
	fdt_imgs = fu_firmware_get_images(FU_FIRMWARE(fdt_root));
	for (guint i = 0; i < fdt_imgs->len; i++) {
		FuFdtImage *fdt_node = g_ptr_array_index(fdt_imgs, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_vbe_plugin_coldplug_img(plugin, fdt_root, fdt_node, &error_local)) {
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
fu_vbe_plugin_get_bfname(FuPlugin *plugin)
{
	FuVbePlugin *self = FU_VBE_PLUGIN(plugin);
	g_autofree gchar *bfname_local = NULL;
	g_autofree gchar *bfname_sys = NULL;
	g_autofree gchar *sysfsdir = NULL;

	/* look for override first, fall back to system value */
	bfname_local = g_build_filename(self->vbe_dir, "system.dtb", NULL);
	if (g_file_test(bfname_local, G_FILE_TEST_EXISTS))
		return g_file_new_for_path(bfname_local);

	/* actual hardware value */
	sysfsdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	bfname_sys = g_build_filename(sysfsdir, "fdt", NULL);
	return g_file_new_for_path(bfname_sys);
}

static gboolean
fu_vbe_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuVbePlugin *self = FU_VBE_PLUGIN(plugin);
	g_autoptr(GFile) file = NULL;

	/* look for override first, fall back to system value */
	file = fu_vbe_plugin_get_bfname(plugin);
	if (!fu_firmware_parse_file(self->fdt, file, FWUPD_INSTALL_FLAG_NO_SEARCH, error)) {
		g_prefix_error(error, "failed to parse FDT: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_vbe_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuVbePlugin *self = FU_VBE_PLUGIN(plugin);
	fu_string_append(str, idt, "VbeDir", self->vbe_dir);
}

static void
fu_vbe_plugin_init(FuVbePlugin *self)
{
	g_autofree gchar *localstatedir_pkg = NULL;

	/* where we can store the override and also image state */
	localstatedir_pkg = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	self->vbe_dir = g_build_filename(localstatedir_pkg, "vbe", NULL);
	self->fdt = fu_fdt_firmware_new();
}

static void
fu_vbe_finalize(GObject *obj)
{
	FuVbePlugin *self = FU_VBE_PLUGIN(obj);
	g_free(self->vbe_dir);
	g_object_unref(self->fdt);
	G_OBJECT_CLASS(fu_vbe_plugin_parent_class)->finalize(obj);
}

static void
fu_vbe_plugin_class_init(FuVbePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_vbe_finalize;
	plugin_class->to_string = fu_vbe_plugin_to_string;
	plugin_class->startup = fu_vbe_plugin_startup;
	plugin_class->coldplug = fu_vbe_plugin_coldplug;
}
