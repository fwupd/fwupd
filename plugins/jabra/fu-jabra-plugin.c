/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jabra-device.h"
#include "fu-jabra-plugin.h"

struct _FuJabraPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuJabraPlugin, fu_jabra_plugin, FU_TYPE_PLUGIN)

/* slightly weirdly, this takes us from appIDLE back into the actual
 * runtime mode where the device actually works */
static gboolean
fu_jabra_plugin_cleanup(FuPlugin *plugin,
			FuDevice *device,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error)
{
	GUsbDevice *usb_device;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* check for a property on the *dfu* FuDevice, which is also why we
	 * can't just rely on using FuDevice->cleanup() */
	if (!fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_ATTACH_EXTRA_RESET))
		return TRUE;
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	if (!g_usb_device_reset(usb_device, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot reset USB device: %s [%i]",
			    error_local->message,
			    error_local->code);
		return FALSE;
	}

	/* wait for device to re-appear */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_jabra_plugin_init(FuJabraPlugin *self)
{
}

static void
fu_jabra_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "JabraMagic");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_JABRA_DEVICE);
}

static void
fu_jabra_plugin_class_init(FuJabraPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_jabra_plugin_constructed;
	plugin_class->cleanup = fu_jabra_plugin_cleanup;
}
