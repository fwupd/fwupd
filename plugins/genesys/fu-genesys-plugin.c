/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-genesys-hubhid-device.h"
#include "fu-genesys-plugin.h"
#include "fu-genesys-scaler-device.h"
#include "fu-genesys-scaler-firmware.h"
#include "fu-genesys-usbhub-codesign-firmware.h"
#include "fu-genesys-usbhub-dev-firmware.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-pd-firmware.h"

struct _FuGenesysPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGenesysPlugin, fu_genesys_plugin, FU_TYPE_PLUGIN)

static void
fu_genesys_plugin_init(FuGenesysPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_genesys_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "GenesysScalerCfiFlashId");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioOutputRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioEnableRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioValue");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubReadRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubSwitchRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubWriteRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubHidProxyId");
	fu_context_add_quirk_key(ctx, "GenesysParentHubPid");
	fu_context_add_quirk_key(ctx, "GenesysRootHubPid");
	fu_context_add_quirk_key(ctx, "GenesysSupportDualBank");
	fu_context_add_quirk_key(ctx, "GenesysSupportCodeSize");
	fu_context_add_quirk_key(ctx, "GenesysHubBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysHubBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysHubBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysDevBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysDevBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysDevBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysPdBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysPdBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysPdBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysCodesignBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysCodesignBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysCodesignBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysFwDataMaxCount");
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_GENESYS_USBHUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GENESYS_HUBHID_DEVICE); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GENESYS_SCALER_DEVICE); /* coverage */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_GENESYS_USBHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_GENESYS_SCALER_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_GENESYS_USBHUB_PD_FIRMWARE); /* coverage */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_GENESYS_USBHUB_CODESIGN_FIRMWARE); /* cov */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_GENESYS_USBHUB_DEV_FIRMWARE); /* coverage */

	/* chain up to parent */
	G_OBJECT_CLASS(fu_genesys_plugin_parent_class)->constructed(obj);
}

static FuDevice *
fu_genesys_plugin_get_device_by_physical_id(FuPlugin *self, const gchar *physical_id)
{
	GPtrArray *devices = fu_plugin_get_devices(self);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (!FU_IS_GENESYS_USBHUB_DEVICE(dev))
			continue;
		if (fu_device_has_private_flag(dev, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED))
			continue;
		if (g_strcmp0(fu_device_get_physical_id(dev), physical_id) == 0)
			return dev;
	}
	return NULL;
}

/* link cascaded hubs by physical ID so fwupd <requires><firmware depth=...> resolves */
static gboolean
fu_genesys_plugin_link_hub(FuPlugin *self, FuDevice *hub)
{
	FuDevice *existing_parent;
	FuDevice *parent;
	const gchar *usb_parent_phys;
	guint16 parent_pid;
	g_autoptr(FuDevice) usb_parent = NULL;
	g_autoptr(GError) error_local = NULL;

	if (fu_device_has_private_flag(hub, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED))
		return FALSE;
	parent_pid = fu_genesys_usbhub_device_get_parent_hub_pid(FU_GENESYS_USBHUB_DEVICE(hub));
	if (parent_pid == 0)
		return FALSE;

	usb_parent = fu_device_get_backend_parent(hub, &error_local);
	if (usb_parent == NULL) {
		g_warning("hub cannot find its upstream hub: %s", error_local->message);
		return FALSE;
	}
	usb_parent_phys = fu_device_get_physical_id(usb_parent);
	if (usb_parent_phys == NULL || usb_parent_phys[0] == '\0') {
		g_warning("hub upstream physical ID cannot be resolved");
		return FALSE;
	}

	/* keep an existing parent only when connected and still matching the current USB parent */
	existing_parent = fu_device_get_parent(hub, NULL);
	if (existing_parent != NULL) {
		const gchar *existing_phys = fu_device_get_physical_id(existing_parent);
		if (!fu_device_has_private_flag(existing_parent,
						FU_DEVICE_PRIVATE_FLAG_UNCONNECTED) &&
		    fu_device_get_vid(existing_parent) == fu_device_get_vid(usb_parent) &&
		    fu_device_get_pid(existing_parent) == fu_device_get_pid(usb_parent) &&
		    g_strcmp0(existing_phys, usb_parent_phys) == 0) {
			if (g_strcmp0(fu_device_get_composite_id(hub),
				      fu_device_get_composite_id(existing_parent)) != 0) {
				fwupd_device_set_composite_id(
				    FWUPD_DEVICE(hub),
				    fu_device_get_composite_id(existing_parent));
				return TRUE;
			}
			return FALSE;
		}
		g_object_set(hub, "parent", NULL, NULL);
		fwupd_device_set_composite_id(FWUPD_DEVICE(hub), fu_device_get_id(hub));
	}

	/* the upstream hub may be added later, which links it from there */
	parent = fu_genesys_plugin_get_device_by_physical_id(self, usb_parent_phys);
	if (parent == NULL || parent == hub)
		return existing_parent != NULL;
	if (!fu_genesys_usbhub_device_check_parent_hub(parent_pid,
						       fu_device_get_vid(hub),
						       parent,
						       &error_local)) {
		g_warning("hub not linked to its upstream hub: %s", error_local->message);
		return existing_parent != NULL;
	}
	g_object_set(hub, "parent", parent, NULL);
	return TRUE;
}

static void
fu_genesys_plugin_device_added(FuPlugin *self, FuDevice *device)
{
	GPtrArray *devices;

	if (!FU_IS_GENESYS_USBHUB_DEVICE(device))
		return;

	/* link hubs and propagate CompositeId to a fixed point regardless of arrival order */
	devices = fu_plugin_get_devices(self);
	for (guint pass = 0; pass < devices->len; pass++) {
		gboolean changed = FALSE;
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *dev = g_ptr_array_index(devices, i);
			if (FU_IS_GENESYS_USBHUB_DEVICE(dev) &&
			    fu_genesys_plugin_link_hub(self, dev))
				changed = TRUE;
		}
		if (!changed)
			break;
	}
}

static void
fu_genesys_plugin_class_init(FuGenesysPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_genesys_plugin_constructed;
	plugin_class->device_added = fu_genesys_plugin_device_added;
}
