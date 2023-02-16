/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-pci-bcr-plugin.h"

struct _FuPciBcrPlugin {
	FuPlugin parent_instance;
	gboolean has_device;
	guint8 bcr_addr;
	guint8 bcr;
};

G_DEFINE_TYPE(FuPciBcrPlugin, fu_pci_bcr_plugin, FU_TYPE_PLUGIN)

#define BCR_WPD	    (1 << 0)
#define BCR_BLE	    (1 << 1)
#define BCR_SMM_BWP (1 << 5)

static void
fu_pci_bcr_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuPciBcrPlugin *self = FU_PCI_BCR_PLUGIN(plugin);
	fu_string_append_kb(str, idt, "HasDevice", self->has_device);
	fu_string_append_kx(str, idt, "BcrAddr", self->bcr_addr);
	fu_string_append_kx(str, idt, "Bcr", self->bcr);
}

static void
fu_pci_bcr_plugin_set_updatable(FuPlugin *plugin, FuDevice *dev)
{
	FuPciBcrPlugin *self = FU_PCI_BCR_PLUGIN(plugin);
	if ((self->bcr & BCR_WPD) == 0 && (self->bcr & BCR_BLE) > 0) {
		fu_device_inhibit(dev, "bcr-locked", "BIOS locked");
	} else {
		fu_device_uninhibit(dev, "bcr-locked");
	}
}

static void
fu_pci_bcr_plugin_device_registered(FuPlugin *plugin, FuDevice *dev)
{
	FuPciBcrPlugin *self = FU_PCI_BCR_PLUGIN(plugin);
	if (g_strcmp0(fu_device_get_plugin(dev), "cpu") == 0 ||
	    g_strcmp0(fu_device_get_plugin(dev), "flashrom") == 0) {
		guint tmp = fu_device_get_metadata_integer(dev, "PciBcrAddr");
		if (tmp != G_MAXUINT && self->bcr_addr != tmp) {
			g_debug("overriding BCR addr from 0x%02x to 0x%02x", self->bcr_addr, tmp);
			self->bcr_addr = tmp;
		}
	}
	if (g_strcmp0(fu_device_get_plugin(dev), "flashrom") == 0 &&
	    fu_device_has_instance_id(dev, "main-system-firmware")) {
		/* PCI\VEN_8086 added first */
		if (self->has_device) {
			fu_pci_bcr_plugin_set_updatable(plugin, dev);
			return;
		}
		fu_plugin_cache_add(plugin, "main-system-firmware", dev);
	}
}

static void
fu_plugin_add_security_attr_bioswe(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciBcrPlugin *self = FU_PCI_BCR_PLUGIN(plugin);
	FuDevice *msf_device = fu_plugin_cache_lookup(plugin, "main-system-firmware");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE);
	if (msf_device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(msf_device));
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (!self->has_device) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* load file */
	if ((self->bcr & BCR_WPD) == 1) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_plugin_add_security_attr_ble(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciBcrPlugin *self = FU_PCI_BCR_PLUGIN(plugin);
	FuDevice *msf_device = fu_plugin_cache_lookup(plugin, "main-system-firmware");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SPI_BLE);
	if (msf_device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(msf_device));
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (!self->has_device) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* load file */
	if ((self->bcr & BCR_BLE) == 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_plugin_add_security_attr_smm_bwp(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciBcrPlugin *self = FU_PCI_BCR_PLUGIN(plugin);
	FuDevice *msf_device = fu_plugin_cache_lookup(plugin, "main-system-firmware");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP);
	if (msf_device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(msf_device));
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (!self->has_device) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* load file */
	if ((self->bcr & BCR_SMM_BWP) == 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static gboolean
fu_pci_bcr_plugin_backend_device_added(FuPlugin *plugin,
				       FuDevice *device,
				       FuProgress *progress,
				       GError **error)
{
	FuPciBcrPlugin *self = FU_PCI_BCR_PLUGIN(plugin);
	FuDevice *device_msf;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* not supported */
	if (self->bcr_addr == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "BCR not supported on this platform");
		return FALSE;
	}

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "pci") != 0)
		return TRUE;

	/* open the config */
	fu_udev_device_set_flags(FU_UDEV_DEVICE(device), FU_UDEV_DEVICE_FLAG_USE_CONFIG);
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "pci", error))
		return FALSE;
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* grab BIOS Control Register */
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(device), self->bcr_addr, &self->bcr, 1, error)) {
		g_prefix_error(error, "could not read BCR: ");
		return FALSE;
	}

	/* main-system-firmware device added first, probably from flashrom */
	device_msf = fu_plugin_cache_lookup(plugin, "main-system-firmware");
	if (device_msf != NULL)
		fu_pci_bcr_plugin_set_updatable(plugin, device_msf);

	/* success */
	self->has_device = TRUE;
	return TRUE;
}

static void
fu_pci_bcr_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	/* only Intel */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	/* add attrs */
	fu_plugin_add_security_attr_bioswe(plugin, attrs);
	fu_plugin_add_security_attr_ble(plugin, attrs);
	fu_plugin_add_security_attr_smm_bwp(plugin, attrs);
}

static void
fu_pci_bcr_plugin_init(FuPciBcrPlugin *self)
{
	/* this is true except for some Atoms */
	self->bcr_addr = 0xdc;
}

static void
fu_pci_bcr_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "PciBcrAddr");
	fu_plugin_add_udev_subsystem(plugin, "pci");
}

static void
fu_pci_bcr_plugin_class_init(FuPciBcrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_pci_bcr_plugin_constructed;
	plugin_class->to_string = fu_pci_bcr_plugin_to_string;
	plugin_class->add_security_attrs = fu_pci_bcr_plugin_add_security_attrs;
	plugin_class->device_registered = fu_pci_bcr_plugin_device_registered;
	plugin_class->backend_device_added = fu_pci_bcr_plugin_backend_device_added;
}
