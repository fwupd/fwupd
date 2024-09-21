/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bcm57xx-device.h"
#include "fu-bcm57xx-dict-image.h"
#include "fu-bcm57xx-firmware.h"
#include "fu-bcm57xx-plugin.h"
#include "fu-bcm57xx-recovery-device.h"
#include "fu-bcm57xx-stage1-image.h"
#include "fu-bcm57xx-stage2-image.h"

struct _FuBcm57XxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuBcm57XxPlugin, fu_bcm57xx_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_bcm57xx_plugin_backend_device_added(FuPlugin *plugin,
				       FuDevice *device,
				       FuProgress *progress,
				       GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* only enumerate number 0 */
	if (fu_udev_device_get_number(FU_UDEV_DEVICE(device)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only device 0 supported on multi-device card");
		return FALSE;
	}

	/* is in recovery mode if has no ethtool interface */
	fn = g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)), "net", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_debug("waiting for net devices to appear");
		fu_device_sleep(device, 50); /* ms */
	}
	ifaces = fu_path_glob(fn, "en*", NULL);
	if (ifaces == NULL || ifaces->len == 0) {
		dev = g_object_new(FU_TYPE_BCM57XX_RECOVERY_DEVICE, NULL);
	} else {
		g_autofree gchar *ethtool_iface = g_path_get_basename(g_ptr_array_index(ifaces, 0));
		dev = g_object_new(FU_TYPE_BCM57XX_DEVICE, "iface", ethtool_iface, NULL);
	}
	fu_device_incorporate(dev, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
	locker = fu_device_locker_new(dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add(plugin, dev);
	return TRUE;
}

static void
fu_bcm57xx_plugin_init(FuBcm57XxPlugin *self)
{
}

static void
fu_bcm57xx_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "bcm57xx");
}

static void
fu_bcm57xx_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_BCM57XX_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_BCM57XX_RECOVERY_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_DICT_IMAGE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_STAGE1_IMAGE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_BCM57XX_STAGE2_IMAGE);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_BETTER_THAN, "optionrom");
}

static void
fu_bcm57xx_plugin_class_init(FuBcm57XxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_bcm57xx_plugin_object_constructed;
	plugin_class->constructed = fu_bcm57xx_plugin_constructed;
	plugin_class->backend_device_added = fu_bcm57xx_plugin_backend_device_added;
}
