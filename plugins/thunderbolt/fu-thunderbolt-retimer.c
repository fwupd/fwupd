/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-thunderbolt-common.h"
#include "fu-thunderbolt-retimer.h"

struct _FuThunderboltRetimer {
	FuThunderboltDevice parent_instance;
};

G_DEFINE_TYPE(FuThunderboltRetimer, fu_thunderbolt_retimer, FU_TYPE_THUNDERBOLT_DEVICE)

gboolean
fu_thunderbolt_retimer_set_parent_port_offline(FuDevice *device, GError **error)
{
	g_autoptr(FuDevice) parent =
	    fu_device_get_backend_parent_with_subsystem(device,
							"thunderbolt:thunderbolt_domain",
							error);
	if (parent == NULL)
		return FALSE;
	if (!fu_thunderbolt_udev_set_port_offline(FU_UDEV_DEVICE(parent), error))
		return FALSE;
	return fu_thunderbolt_udev_rescan_port(FU_UDEV_DEVICE(parent), error);
}

gboolean
fu_thunderbolt_retimer_set_parent_port_online(FuDevice *device, GError **error)
{
	g_autoptr(FuDevice) parent =
	    fu_device_get_backend_parent_with_subsystem(device,
							"thunderbolt:thunderbolt_domain",
							error);
	if (parent == NULL)
		return FALSE;
	return fu_thunderbolt_udev_set_port_online(FU_UDEV_DEVICE(parent), error);
}

static gboolean
fu_thunderbolt_retimer_probe(FuDevice *device, GError **error)
{
	const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	g_autofree gchar *physical_id = g_path_get_basename(devpath);

	/* device */
	if (physical_id != NULL)
		fu_device_set_physical_id(device, physical_id);

	return TRUE;
}

static gboolean
fu_thunderbolt_retimer_reload(FuDevice *device, GError **error)
{
	/* get version */
	if (!fu_thunderbolt_udev_rescan_port(FU_UDEV_DEVICE(device), error))
		return FALSE;
	if (!fu_thunderbolt_device_get_version(FU_THUNDERBOLT_DEVICE(device), error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_thunderbolt_retimer_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* FuThunderboltDevice->attach for nvm_authenticate */
	if (!FU_DEVICE_CLASS(fu_thunderbolt_retimer_parent_class)->attach(device, progress, error))
		return FALSE;

	/* online */
	fu_device_sleep(device, FU_THUNDERBOLT_RETIMER_CLEANUP_DELAY);
	if (!fu_thunderbolt_retimer_set_parent_port_online(device, error))
		return FALSE;

	/* retimer gets removed, which we ignore, due to no-auto-remove */
	fu_device_sleep(device, 1000);

	/* get the new retimer firmware version by rescanning */
	if (!fu_thunderbolt_retimer_set_parent_port_offline(device, error))
		return FALSE;

	/* wait for it to re-appear */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
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
	vid = fu_device_get_vid(FU_DEVICE(self));
	if (vid == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "missing vendor id");
		return FALSE;
	}
	did = fu_device_get_pid(device);
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
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE);
}

static void
fu_thunderbolt_retimer_class_init(FuThunderboltRetimerClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_thunderbolt_retimer_attach;
	device_class->setup = fu_thunderbolt_retimer_setup;
	device_class->reload = fu_thunderbolt_retimer_reload;
	device_class->probe = fu_thunderbolt_retimer_probe;
}
