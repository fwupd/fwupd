/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodixtp-brlb-device.h"
#include "fu-goodixtp-common.h"
#include "fu-goodixtp-firmware.h"
#include "fu-goodixtp-gtx8-device.h"
#include "fu-goodixtp-plugin.h"
#include "fu-goodixtp-struct.h"

struct _FuGoodixtpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpPlugin, fu_goodixtp_plugin, FU_TYPE_PLUGIN)

static void
fu_goodixtp_plugin_init(FuGoodixtpPlugin *self)
{
}

static void
fu_goodixtp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_GOODIXTP_HID_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_GOODIXTP_FIRMWARE);
}

static FuGoodixtpIcType
fu_goodixtp_plugin_ic_type_from_pid(guint16 pid)
{
	if ((pid >= 0x01E0 && pid <= 0x01E7) || (pid >= 0x0D00 && pid <= 0x0D7F))
		return FU_GOODIXTP_IC_TYPE_NORMANDYL;
	if ((pid >= 0x0EB0 && pid <= 0x0EBF) || (pid >= 0x0EC0 && pid <= 0x0ECF) ||
	    (pid >= 0x0EA5 && pid <= 0x0EAA) || (pid >= 0x0C00 && pid <= 0x0CFF))
		return FU_GOODIXTP_IC_TYPE_BERLINB;

	return FU_GOODIXTP_IC_TYPE_NONE;
}

static gboolean
fu_goodixtp_plugin_backend_device_added(FuPlugin *plugin,
					FuDevice *device,
					FuProgress *progress,
					GError **error)
{
	FuGoodixtpIcType ic_type;
	g_autoptr(FuDeviceLocker) locker = NULL;

	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	ic_type = fu_goodixtp_plugin_ic_type_from_pid(fu_device_get_pid(device));
	if (ic_type == FU_GOODIXTP_IC_TYPE_NORMANDYL) {
		g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_GOODIXTP_GTX8_DEVICE,
						       "context",
						       fu_plugin_get_context(plugin),
						       NULL);
		fu_device_incorporate(dev, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
		locker = fu_device_locker_new(dev, error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_device_add(plugin, dev);
		return TRUE;
	}

	if (ic_type == FU_GOODIXTP_IC_TYPE_BERLINB) {
		g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_GOODIXTP_BRLB_DEVICE,
						       "context",
						       fu_plugin_get_context(plugin),
						       NULL);
		fu_device_incorporate(dev, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
		locker = fu_device_locker_new(dev, error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_device_add(plugin, dev);
		return TRUE;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "can't find valid ic_type, pid is %x",
		    fu_device_get_pid(device));
	return FALSE;
}

static void
fu_goodixtp_plugin_class_init(FuGoodixtpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_goodixtp_plugin_constructed;
	plugin_class->backend_device_added = fu_goodixtp_plugin_backend_device_added;
}
