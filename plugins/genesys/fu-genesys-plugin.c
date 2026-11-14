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

static void
fu_genesys_plugin_class_init(FuGenesysPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_genesys_plugin_constructed;
}
