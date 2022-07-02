/*
 * Plugin to handle VBE updates
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <libfdt.h>

#include "fu-vbe-device.h"
#include "fu-vbe-simple-device.h"

/* kernel device tree, used for system information */
#define KERNEL_DT "/sys/firmware/fdt"

/* file to use for system information where the system has no device tree */
#define SYSTEM_DT "system.dtb"

/* path to the method subnodes in the system info */
#define NODE_PATH "/chosen/fwupd"

/**
 * This stores information used by all VBE methods
 *
 * @fdt: Device tree containing the info
 * @vbe_dir: Path to the VBE directory, e.g /var/local/lib/fwupd/vbe
 * @methods: List of methods that are being used, each a FuVbeMethod
 */
struct FuPluginData {
	gchar *fdt;
	gchar *vbe_dir;
	GList *methods;
};

/** Information about available VBE drivers
 *
 * @name: Name of driver (for compatible string "fwupd,vbe-simple" this is
 * "simple")
 * @vendor: Vendor name
 * @vendor_id: Vendor ID, with have a "VBE:" prefix
 * @version: Version of this driver in x.y.z numeric notation
 * @version_lowest: Lowest version of firmware this device can accept
 * @new_func: Function to call to create the device
 * @guid: GUID to use to identify this device and updates intended for it
 */
struct VbeDriver {
	const gchar *name;
	const gchar *vendor;
	const gchar *vendor_id;
	const gchar *version_lowest;
	vbe_device_new_func new_func;
	const gchar *guid;
};

/** List of available VBE drivers */
const struct VbeDriver driver_list[] = {
    {"simple",
     "U-Boot",
     "VBE:U-Boot",
     "0.0.1",
     fu_vbe_simple_device_new,
     "bb3b05a8-ebef-11ec-be98-d3a15278be95"},
    {NULL},
};

/** Information about an update method with an associated device
 * @vbe_method: Method name, e.g. "simple" if compatible is "fwupd,vbe-simple"
 * @node: Offset of this method in device tree (so it can read its info)
 */
struct FuVbeMethod {
	const gchar *vbe_method;
	gint node;
	const struct VbeDriver *driver;
};

static void
fu_plugin_vbe_init(FuPlugin *plugin)
{
	(void)fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
}

static void
fu_plugin_vbe_destroy(FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_free(priv->vbe_dir);
	g_free(priv->fdt);
	g_list_free_full(priv->methods, g_free);
}

/**
 * fu_plugin_vbe_locate_method() - Locate the method to use for a particular node
 *
 * This checks the compatible string in the format fwupd,vbe-xxx and finds the
 * driver called xxx.
 *
 * @fdt: Device tree to use
 * @node: Node to use
 * @methp: Returns the method associated with that node, if any, NULL on failure
 * @error: Returns an error if something went wrong
 * Returns: True on success, False on failure
 */
static gboolean
fu_plugin_vbe_locate_method(gchar *fdt, gint node, struct FuVbeMethod **methp, GError **error)
{
	struct FuVbeMethod *meth = NULL;
	const struct VbeDriver *driver;
	const gchar *method_name;
	const gchar *compat;
	gint len;
	g_auto(GStrv) split = NULL;

	/* we expect 'fwupd,vbe-<driver>' */
	*methp = NULL;
	compat = fdt_getprop(fdt, node, "compatible", &len);
	if (compat == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "missing update mechanism (%s)",
			    fdt_strerror(len));
		return FALSE;
	}
	split = g_strsplit(compat, ",", 2);
	if (g_strv_length(split) != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "update mechanism is missing manufacturer: %s",
			    compat);
		return FALSE;
	}
	if (g_strcmp0(split[0], "fwupd") == 0) {
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
	method_name = split[1] + 4;

	/* find this update mechanism */
	for (driver = driver_list; driver->name; driver++) {
		if (!strcmp(method_name, driver->name)) {
			meth = g_malloc(sizeof(struct FuVbeMethod));
			meth->vbe_method = method_name;
			meth->node = node;
			meth->driver = driver;
			g_debug("Update mechanism: %s", meth->vbe_method);
			*methp = meth;
			return TRUE;
		}
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "no driver for VBE method '%s'",
		    method_name);
	return FALSE;
}

static gboolean
fu_plugin_vbe_process_system(FuPluginData *priv, gchar *fdt, gsize fdt_len, GError **error)
{
	gint rc, parent, node;
	gint found;

	rc = fdt_check_header(fdt);
	if (rc != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "system DT is corrupt (%s)",
			    fdt_strerror(rc));
		return FALSE;
	}
	if (fdt_totalsize(fdt) != fdt_len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "system DT size mismatch (header=0x%x, file=0x%zx)",
			    fdt_totalsize(fdt),
			    fdt_len);
		return FALSE;
	}
	parent = fdt_path_offset(fdt, NODE_PATH);
	if (parent < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "missing node '%s' (%s)",
			    NODE_PATH,
			    fdt_strerror(rc));
		return FALSE;
	}

	/* create a device for each subnode */
	found = 0;
	for (node = fdt_first_subnode(fdt, parent); node > 0; node = fdt_next_subnode(fdt, node)) {
		struct FuVbeMethod *meth;
		g_autoptr(GError) error_local = NULL;

		if (fu_plugin_vbe_locate_method(fdt, node, &meth, &error_local)) {
			found++;
		} else {
			g_debug("Cannot locate device for node '%s': %s",
				fdt_get_name(fdt, node, NULL),
				error_local->message);
		}
		priv->methods = g_list_append(priv->methods, meth);
	}

	if (!found) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no valid VBE update mechanism found");
		return FALSE;
	}

	g_debug("VBE update methods: %d", found);
	return TRUE;
}

static gboolean
fu_plugin_vbe_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	g_autofree gchar *state_dir = NULL;
	g_autofree gchar *bfname = NULL;
	g_autoptr(GError) error_local = NULL;
	gchar *buf = NULL;
	gsize len;

	fu_progress_set_id(progress, G_STRLOC);

	/* get the VBE directory */
	state_dir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	priv->vbe_dir = g_build_filename(state_dir, "vbe", NULL);
	bfname = g_build_filename(priv->vbe_dir, SYSTEM_DT, NULL);
	if (!g_file_get_contents(bfname, &buf, &len, &error_local)) {
		/* check if we have a kernel device tree */
		g_debug("Cannot find system DT '%s': %s", bfname, error_local->message);

		/* free the filename so we can reuse it */
		g_free(bfname);

		/* read in the system info */
		bfname = g_build_filename(KERNEL_DT, NULL);
		if (!g_file_get_contents(bfname, &buf, &len, error)) {
			g_prefix_error(error, "No kernel device tree '%s': ", bfname);
			return FALSE;
		}
	}
	g_debug("Processing system DT '%s'", bfname);
	priv->fdt = buf;
	if (!fu_plugin_vbe_process_system(priv, buf, len, error)) {
		g_prefix_error(error, "failed to parse: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_plugin_vbe_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuPluginData *priv = fu_plugin_get_data(plugin);
	struct FuVbeMethod *meth;
	GList *entry;

	/* create a driver for each method */
	for (entry = g_list_first(priv->methods); entry; entry = g_list_next(entry)) {
		const struct VbeDriver *driver;
		FuDevice *dev;
		const gchar *version;

		meth = entry->data;
		driver = meth->driver;
		dev = driver->new_func(ctx, meth->vbe_method, priv->fdt, meth->node, priv->vbe_dir);

		fu_device_set_id(dev, meth->vbe_method);

		fu_device_set_name(dev, driver->name);
		fu_device_set_vendor(dev, driver->vendor);
		fu_device_add_guid(dev, driver->guid);

		fu_device_add_vendor_id(FU_DEVICE(dev), driver->vendor_id);
		fu_device_set_version_format(dev, FWUPD_VERSION_FORMAT_TRIPLET);
		fu_device_set_version_lowest(dev, driver->version_lowest);

		version = fdt_getprop(priv->fdt, meth->node, "cur-version", NULL);
		fu_device_set_version(dev, version);

		version = fdt_getprop(priv->fdt, meth->node, "bootloader-version", NULL);
		fu_device_set_version_bootloader(dev, version);
		fu_device_add_icon(dev, "computer");
		fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);

		/* this takes a ref on the device */
		fu_plugin_device_add(plugin, dev);
	}

	return TRUE;
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
