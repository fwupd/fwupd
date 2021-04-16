/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-realtek-mst-device.h"

struct _FuRealtekMstDevice {
  FuUdevDevice parent_instance;
  gchar *dp_aux_dev_name;
};

G_DEFINE_TYPE (FuRealtekMstDevice, fu_realtek_mst_device,
	       FU_TYPE_UDEV_DEVICE)

static gboolean fu_realtek_mst_device_set_quirk_kv (FuDevice *device,
						    const gchar *key,
						    const gchar *value,
						    GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);

	if (g_strcmp0(key, "RealtekMstDpAuxName") == 0) {
		self->dp_aux_dev_name = g_strdup (value);
	} else {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "unsupported quirk key: %s", key);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_mst_device_probe (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	const gchar *quirk_name = NULL;
	g_autofree gchar *physical_id = NULL;
	g_autofree gchar *instance_id = NULL;

	if (!FU_DEVICE_CLASS(fu_realtek_mst_device_parent_class)->probe (device, error))
		return FALSE;

	physical_id = g_strdup_printf ("I2C_PATH=%s",
				       fu_udev_device_get_sysfs_path ( FU_UDEV_DEVICE (device)));
	fu_device_set_physical_id (device, physical_id);

	/* set custom instance ID and load matching quirks */
	instance_id = g_strdup_printf ("REALTEK-MST\\Name_%s",
				       fu_udev_device_get_sysfs_attr (
					       FU_UDEV_DEVICE (device),
					       "name",
					       NULL));
	fu_device_add_instance_id (device, instance_id);

	/* having loaded quirks, check this device is supported */
	quirk_name = fu_device_get_name (device);
	if (g_strcmp0 (quirk_name, "RTD2142") != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "only RTD2142 is supported");
		return FALSE;
	}

	if (self->dp_aux_dev_name == NULL) {
		g_set_error_literal (error, FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "RealtekMstDpAuxName must be specified");
		return FALSE;
	}

	/* TODO: find the drm_dp_aux_dev with given name and walk to its sibling
	 * with subsystem=i2c, then the child of that with subsystem=i2c-dev */

	return TRUE;
}

static void
fu_realtek_mst_device_init (FuRealtekMstDevice *self)
{
	FuDevice *device = FU_DEVICE (self);
	self->dp_aux_dev_name = NULL;

	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_PAIR);

	fu_device_add_protocol (device, "com.realtek.rtd2142");
	fu_device_set_vendor (device, "Realtek");
	fu_device_add_vendor_id (device, "PCI:0x10EC");
	fu_device_set_summary (device, "DisplayPort MST hub");
	fu_device_add_icon (device, "video-display");
}

static void
fu_realtek_mst_device_finalize (GObject *object)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (object);
	g_free (self->dp_aux_dev_name);

	G_OBJECT_CLASS (fu_realtek_mst_device_parent_class)->finalize (object);
}

static void
fu_realtek_mst_device_class_init (FuRealtekMstDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	GObjectClass *klass_object = G_OBJECT_CLASS(klass);

	klass_object->finalize = fu_realtek_mst_device_finalize;
	klass_device->probe = fu_realtek_mst_device_probe;
	klass_device->set_quirk_kv = fu_realtek_mst_device_set_quirk_kv;
	/*
	klass_device->open = fu_flashrom_lspcon_i2c_spi_device_open;
	klass_device->setup = fu_flashrom_lspcon_i2c_spi_device_setup;
	klass_device->write_firmware = fu_flashrom_lspcon_i2c_spi_device_write_firmware;
	 */
}
