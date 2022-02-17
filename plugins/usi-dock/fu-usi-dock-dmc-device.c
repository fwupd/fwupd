/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-usi-dock-dmc-device.h"

struct _FuUsiDockDmcDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuUsiDockDmcDevice, fu_usi_dock_dmc_device, FU_TYPE_USB_DEVICE)

static void
fu_usi_dock_dmc_device_parent_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	FuDevice *parent = fu_device_get_parent(device);
	if (parent != NULL) {
		g_autoptr(GError) error = NULL;

		/* slightly odd: the MCU device uses the DMC version number */
		g_debug("absorbing DMC version into MCU");
		fu_device_set_version_format(parent, fu_device_get_version_format(device));
		fu_device_set_version(parent, fu_device_get_version(device));
		fu_device_set_serial(parent, fu_device_get_serial(device));

		/* allow matching firmware */
		fu_device_add_instance_str(parent, "CID", fu_device_get_name(device));
		if (!fu_device_build_instance_id(parent, &error, "USB", "VID", "PID", "CID", NULL))
			g_warning("failed to build ID: %s", error->message);

		/* don't allow firmware updates on this */
		fu_device_set_name(device, "Dock Management Controller Information");
		fu_device_inhibit(device, "dummy", "Use the MCU to update the DMC device");
	}
}

static void
fu_usi_dock_dmc_device_init(FuUsiDockDmcDevice *self)
{
	g_signal_connect(FU_DEVICE(self),
			 "notify::parent",
			 G_CALLBACK(fu_usi_dock_dmc_device_parent_notify_cb),
			 NULL);
}

static void
fu_usi_dock_dmc_device_class_init(FuUsiDockDmcDeviceClass *klass)
{
}
