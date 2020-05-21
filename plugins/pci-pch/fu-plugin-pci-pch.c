/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

typedef union {
	guint32 data;
	struct {
		guint32 locked			: 1;
		guint32 rsrvd			: 3;
		guint32 enabled			: 1;
	} __attribute__((packed)) fields;
} FuPchDciECTRL;

struct FuPluginData {
	gboolean		 has_device;
	FuPchDciECTRL		 dci_ectrl;
};

#define PCI_CFG_DCI		0xB8

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "pci");
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	guint8 buf[4] = { 0x0 };
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "pci") != 0)
		return TRUE;

	/* open the config */
	fu_udev_device_set_flags (device, FU_UDEV_DEVICE_FLAG_USE_CONFIG);
	if (!fu_udev_device_set_physical_id (device, "pci", error))
		return FALSE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* grab PCH config Register */
	if (!fu_udev_device_pread_full (device, PCI_CFG_DCI, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read PCH DCI");
		return FALSE;
	}
	priv->dci_ectrl.data = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	priv->has_device = TRUE;
	return TRUE;
}

static void
fu_plugin_add_security_attr_dci_enabled (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_DCI_ENABLED);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* check fields */
	if (priv->dci_ectrl.fields.enabled) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_plugin_add_security_attr_dci_locked (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_DCI_LOCKED);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append (attrs, attr);

	/* check fields */
	if (!priv->dci_ectrl.fields.locked) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	/* only Intel */
	if (!fu_common_is_cpu_intel ())
		return;
	if (!priv->has_device)
		return;

	fu_plugin_add_security_attr_dci_enabled (plugin, attrs);
	fu_plugin_add_security_attr_dci_locked (plugin, attrs);
}
