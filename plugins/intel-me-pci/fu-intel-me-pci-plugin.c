/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-me-pci-plugin.h"

struct _FuIntelMePciPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIntelMePciPlugin, fu_intel_me_pci_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_intel_me_pci_plugin_ensure_version(FuIntelMePciPlugin *self,
				      FuIntelMeDevice *me_device,
				      FuDevice *pci_device,
				      GError **error)
{
	g_auto(GStrv) lines = NULL;
	g_auto(GStrv) sections = NULL;
	g_autofree gchar *fwvers = NULL;

	/* we only care about the first version */
	fwvers = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(pci_device),
					   "mei/mei0/fw_ver",
					   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					   error);
	if (fwvers == NULL)
		return FALSE;
	lines = g_strsplit(fwvers, "\n", -1);
	if (g_strv_length(lines) < 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "expected data, got %s",
			    fwvers);
		return FALSE;
	}

	/* split platform : version */
	sections = g_strsplit(lines[0], ":", -1);
	if (g_strv_length(sections) != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "expected platform:major.minor.micro.build, got %s",
			    lines[0]);
		return FALSE;
	}
	fu_device_set_version(FU_DEVICE(me_device), sections[1]);
	return TRUE;
}

static gboolean
fu_intel_me_pci_plugin_backend_device_added(FuPlugin *plugin,
					    FuDevice *device,
					    FuProgress *progress,
					    GError **error)
{
	FuIntelMePciPlugin *self = FU_INTEL_ME_PCI_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	/* nocheck:magic */
	const guint hfs_cfg_addrs[] = {0x0, 0x40, 0x48, 0x60, 0x64, 0x68, 0x6c};
	g_autofree gchar *device_file = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuIntelMeDevice) me_device = fu_intel_me_device_new(ctx);

	/* interesting device? */
	if (!FU_IS_PCI_DEVICE(device))
		return TRUE;

	/* open the config */
	device_file =
	    g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)), "config", NULL);
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(device), device_file);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(device), FU_IO_CHANNEL_OPEN_FLAG_READ);
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* grab MEI config registers */
	for (guint i = 1; i < G_N_ELEMENTS(hfs_cfg_addrs); i++) {
		guint8 buf[4] = {0};
		g_autoptr(FuStructIntelMeHfsts) st = NULL;

		if (!fu_udev_device_pread(FU_UDEV_DEVICE(device),
					  hfs_cfg_addrs[i],
					  buf,
					  sizeof(buf),
					  error)) {
			g_prefix_error(error, "could not read HFS%u: ", i);
			return FALSE;
		}
		st = fu_struct_intel_me_hfsts_parse(buf, sizeof(buf), 0x0, error);
		if (st == NULL)
			return FALSE;
		fu_intel_me_device_set_hfsts(me_device, i, st);
	}

	/* set firmware version */
	if (!fu_intel_me_pci_plugin_ensure_version(self, me_device, device, error))
		return FALSE;

	/* success */
	fu_device_set_proxy(FU_DEVICE(me_device), device);
	fu_plugin_add_device(plugin, FU_DEVICE(me_device));
	return TRUE;
}

static void
fu_intel_me_pci_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr_cpu = NULL;

	/* only Intel */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	/* CPU supported */
	attr_cpu = fu_security_attrs_get_by_appstream_id(attrs,
							 FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU,
							 NULL);
	if (attr_cpu != NULL)
		fwupd_security_attr_add_flag(attr_cpu, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_intel_me_pci_plugin_init(FuIntelMePciPlugin *self)
{
}

static void
fu_intel_me_pci_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
}

static void
fu_intel_me_pci_plugin_class_init(FuIntelMePciPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_intel_me_pci_plugin_constructed;
	plugin_class->add_security_attrs = fu_intel_me_pci_plugin_add_security_attrs;
	plugin_class->backend_device_added = fu_intel_me_pci_plugin_backend_device_added;
}
