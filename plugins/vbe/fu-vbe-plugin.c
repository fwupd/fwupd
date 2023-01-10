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
};

G_DEFINE_TYPE(FuVbePlugin, fu_vbe_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_vbe_plugin_coldplug_img(FuVbePlugin *self,
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
			   fu_plugin_get_context(FU_PLUGIN(self)),
			   "fdt-root",
			   fdt_root,
			   "fdt-node",
			   fdt_node,
			   NULL);
	fu_plugin_device_add(FU_PLUGIN(self), dev);
	return TRUE;
}

static gboolean
fu_vbe_plugin_backend_device_added(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuVbePlugin *self = FU_VBE_PLUGIN(plugin);
	g_autoptr(FuFirmware) fdt = NULL;
	g_autoptr(FuFdtImage) fdt_root = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) fdt_imgs = NULL;

	/* not interesting */
	if (g_strcmp0(fu_device_get_backend_id(device), "fdt") != 0)
		return TRUE;

	/* get compatible from root node */
	fdt = fu_device_read_firmware(device, progress, error);
	if (fdt == NULL)
		return FALSE;
	fdt_root = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(fdt), "/chosen/fwupd", error);
	if (fdt_root == NULL)
		return FALSE;
	fdt_imgs = fu_firmware_get_images(FU_FIRMWARE(fdt_root));
	for (guint i = 0; i < fdt_imgs->len; i++) {
		FuFdtImage *fdt_node = g_ptr_array_index(fdt_imgs, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_vbe_plugin_coldplug_img(self, fdt_root, fdt_node, &error_local)) {
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

static void
fu_vbe_plugin_init(FuVbePlugin *self)
{
}

static void
fu_vbe_plugin_class_init(FuVbePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->backend_device_added = fu_vbe_plugin_backend_device_added;
}
