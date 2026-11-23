/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_LINUX_SED_OPAL_H
#include <linux/sed-opal.h>
#endif
#include <sys/ioctl.h>

#include "fu-tcg-opal-device.h"

#define FU_TCG_OPAL_DEVICE_IOCTL_TIMEOUT 500 /* ms */

struct _FuTcgOpalDevice {
	FuUdevDevice parent_instance;
	FuTcgOpalFlags opal_flags;
};

G_DEFINE_TYPE(FuTcgOpalDevice, fu_tcg_opal_device, FU_TYPE_UDEV_DEVICE)

static void
fu_tcg_opal_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuTcgOpalDevice *self = FU_TCG_OPAL_DEVICE(device);
	g_autofree gchar *opal_flags = fu_tcg_opal_flags_to_string(self->opal_flags);
	fwupd_codec_string_append(str, idt, "OpalFlags", opal_flags);
}

FuTcgOpalFlags
fu_tcg_opal_device_get_opal_flags(FuTcgOpalDevice *self)
{
	g_return_val_if_fail(FU_IS_TCG_OPAL_DEVICE(self), 0);
	return self->opal_flags;
}

void
fu_tcg_opal_device_set_opal_flags(FuTcgOpalDevice *self, FuTcgOpalFlags opal_flags)
{
	g_return_if_fail(FU_IS_TCG_OPAL_DEVICE(self));
	self->opal_flags = opal_flags;
}

static gboolean
fu_tcg_opal_device_probe(FuDevice *device, GError **error)
{
	FuTcgOpalDevice *self = FU_TCG_OPAL_DEVICE(device);
	g_autoptr(FuDevice) device_nvme = NULL;

	/* check parent */
	device_nvme = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "nvme", error);
	if (device_nvme == NULL)
		return FALSE;
	if (!fu_device_probe(device_nvme, error))
		return FALSE;
	fu_device_set_physical_id(FU_DEVICE(self), fu_device_get_backend_id(device_nvme));
	fu_device_incorporate(FU_DEVICE(self), device_nvme, FU_DEVICE_INCORPORATE_FLAG_BACKEND_ID);

	/* success */
	return TRUE;
}

static gboolean
fu_tcg_opal_device_setup(FuDevice *device, GError **error)
{
#ifdef HAVE_LINUX_SED_OPAL_H
	FuTcgOpalDevice *self = FU_TCG_OPAL_DEVICE(device);
	struct opal_status status = {0};
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));
	g_autoptr(GError) error_local = NULL;

	if (!fu_ioctl_execute(ioctl,
			      IOC_OPAL_GET_STATUS,
			      (guint8 *)&status,
			      sizeof(status),
			      NULL,
			      FU_TCG_OPAL_DEVICE_IOCTL_TIMEOUT,
			      FU_IOCTL_FLAG_NONE,
			      &error_local)) {
		g_debug("failed to get TCG Opal status: %s", error_local->message);
	} else {
		fu_tcg_opal_device_set_opal_flags(self, status.flags);
	}
#endif

	/* success */
	return TRUE;
}

static void
fu_tcg_opal_device_init(FuTcgOpalDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_DRIVE_HARDDISK);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
}

static void
fu_tcg_opal_device_class_init(FuTcgOpalDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_tcg_opal_device_probe;
	device_class->setup = fu_tcg_opal_device_setup;
	device_class->to_string = fu_tcg_opal_device_to_string;
}
