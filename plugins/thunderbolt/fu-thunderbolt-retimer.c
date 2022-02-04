/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-thunderbolt-common.h"
#include "fu-thunderbolt-retimer.h"

struct _FuThunderboltRetimer {
	FuThunderboltDevice parent_instance;
};

G_DEFINE_TYPE(FuThunderboltRetimer, fu_thunderbolt_retimer, FU_TYPE_THUNDERBOLT_DEVICE)

static FuUdevDevice *
fu_thunderbolt_retimer_get_udev_grandparent(FuDevice *device, GError **error)
{
	g_autoptr(GUdevDevice) udev_parent1 = NULL;
	g_autoptr(GUdevDevice) udev_parent2 = NULL;
	GUdevDevice *udev_device = NULL;
	FuThunderboltRetimer *self = FU_THUNDERBOLT_RETIMER(device);

	udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	if (udev_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get udev device for retimer");
		return NULL;
	}
	udev_parent1 = g_udev_device_get_parent(udev_device);
	if (udev_parent1 == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get parent device for retimer");
		return NULL;
	}
	udev_parent2 = g_udev_device_get_parent(udev_parent1);
	if (udev_parent2 == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get host router device for retimer");
		return NULL;
	}
	return fu_udev_device_new_with_context(fu_device_get_context(FU_DEVICE(self)),
					       g_steal_pointer(&udev_parent2));
}

gboolean
fu_thunderbolt_retimer_set_parent_port_offline(FuDevice *device, GError **error)
{
	g_autoptr(FuUdevDevice) parent = fu_thunderbolt_retimer_get_udev_grandparent(device, error);
	if (parent == NULL)
		return FALSE;
	return fu_thunderbolt_udev_set_port_offline(parent, error);
}

gboolean
fu_thunderbolt_retimer_set_parent_port_online(FuDevice *device, GError **error)
{
	g_autoptr(FuUdevDevice) parent = fu_thunderbolt_retimer_get_udev_grandparent(device, error);
	if (parent == NULL)
		return FALSE;
	return fu_thunderbolt_udev_set_port_online(parent, error);
}

static gboolean
fu_thunderbolt_retimer_probe(FuDevice *device, GError **error)
{
	const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	g_autofree gchar *physical_id = g_path_get_basename(devpath);

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_thunderbolt_retimer_parent_class)->probe(device, error))
		return FALSE;

	/* device */
	if (physical_id != NULL)
		fu_device_set_physical_id(device, physical_id);

	return TRUE;
}

static gboolean
fu_thunderbolt_retimer_setup(FuDevice *device, GError **error)
{
	FuThunderboltRetimer *self = FU_THUNDERBOLT_RETIMER(device);
	guint16 did;
	guint16 vid;
	g_autofree gchar *instance = NULL;

	/* get version */
	if (!fu_thunderbolt_device_get_version(FU_THUNDERBOLT_DEVICE(self), error))
		return FALSE;

	/* as defined in PCIe 4.0 spec */
	vid = fu_udev_device_get_vendor(FU_UDEV_DEVICE(self));
	if (vid == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "missing vendor id");
		return FALSE;
	}
	did = fu_udev_device_get_model(FU_UDEV_DEVICE(self));
	if (did == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "missing device id");
		return FALSE;
	}

	instance = g_strdup_printf("TBT-%04x%04x-retimer%s",
				   (guint)vid,
				   (guint)did,
				   fu_device_get_physical_id(device));
	fu_device_add_instance_id(device, instance);

	/* hardcoded for now:
	 * 1. unsure if ID_VENDOR_FROM_DATABASE works in this instance
	 * 2. we don't recognize anyone else yet
	 */
	if (fu_device_get_vendor(device) == NULL)
		fu_device_set_vendor(device, "Intel");

	/* success */
	return TRUE;
}

static void
fu_thunderbolt_retimer_init(FuThunderboltRetimer *self)
{
	fu_device_set_name(FU_DEVICE(self), "USB4 Retimer");
	fu_device_set_summary(
	    FU_DEVICE(self),
	    "A physical layer protocol-aware, software-transparent extension device "
	    "that forms two separate electrical link segments");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE);
}

static void
fu_thunderbolt_retimer_class_init(FuThunderboltRetimerClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_thunderbolt_retimer_setup;
	klass_device->probe = fu_thunderbolt_retimer_probe;
}
