/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#include <glib/gstdio.h>
#include <linux/i2c-dev.h>

#include "config.h"
#include "fu-realtek-mst-device.h"
#include "rtd2142.h"

struct _FuRealtekMstDevice {
  FuUdevDevice parent_instance;
  gchar *dp_aux_dev_name;
  FuUdevDevice *bus_device;
};

G_DEFINE_TYPE (FuRealtekMstDevice, fu_realtek_mst_device,
	       FU_TYPE_UDEV_DEVICE)

static gboolean fu_realtek_mst_device_set_quirk_kv (FuDevice *device,
						    const gchar *key,
						    const gchar *value,
						    GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);

	if (g_strcmp0 (key, "RealtekMstDpAuxName") == 0) {
		self->dp_aux_dev_name = g_strdup (value);
	} else {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "unsupported quirk key: %s", key);
		return FALSE;
	}
	return TRUE;
}

static FuUdevDevice *fu_realtek_mst_device_locate_bus (FuRealtekMstDevice *self,
						       GError **error)
{
	g_autoptr(GUdevClient) udev_client = g_udev_client_new (NULL);
	g_autoptr(GUdevEnumerator)
		udev_enumerator = g_udev_enumerator_new (udev_client);
	g_autoptr(GList) matches = NULL;
	g_autoptr(FuUdevDevice) bus_device = NULL;

	g_udev_enumerator_add_match_subsystem (udev_enumerator,
					       "drm_dp_aux_dev");
	g_udev_enumerator_add_match_sysfs_attr (udev_enumerator,
						"name",
						self->dp_aux_dev_name);
	matches = g_udev_enumerator_execute (udev_enumerator);

	/* from a drm_dp_aux_dev with the given name, locate its sibling i2c
	 * device and in turn the i2c-dev under that representing the actual
	 * I2C bus that runs over DPDDC on the port represented by the
	 * drm_dp_aux_dev */
	for (GList *element = matches; element != NULL; element = element->next) {
		g_autoptr(FuUdevDevice)
			device = fu_udev_device_new (element->data);
		g_autoptr(GPtrArray) i2c_devices = NULL;

		if (bus_device != NULL) {
			g_debug ("Ignoring additional aux device %s",
				 fu_udev_device_get_sysfs_path (device));
			continue;
		}

		i2c_devices = fu_udev_device_get_siblings_with_subsystem (device, "i2c");
		for (guint i = 0; i < i2c_devices->len; i++) {
			FuUdevDevice *i2c_device = g_ptr_array_index (i2c_devices, i);
			g_autoptr(GPtrArray) i2c_buses =
				fu_udev_device_get_children_with_subsystem (i2c_device, "i2c-dev");

			if (i2c_buses->len == 0) {
				g_debug ("no i2c-dev found under %s",
					 fu_udev_device_get_sysfs_path (i2c_device));
				continue;
			}
			if (i2c_buses->len > 1) {
				g_debug ("ignoring %u additional i2c-dev under %s",
					 i2c_buses->len - 1,
					 fu_udev_device_get_sysfs_path (i2c_device));
			}

			bus_device = g_ptr_array_steal_index_fast (i2c_buses, 0);
			g_debug ("Found I2C bus at %s",
				 fu_udev_device_get_sysfs_path (bus_device));
			break;
		}
	}

	if (bus_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "did not find an i2c-dev associated with DP aux \"%s\"",
			     self->dp_aux_dev_name);
		return NULL;
	}
	return g_steal_pointer (&bus_device);
}

static gboolean
fu_realtek_mst_device_probe (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	const gchar *quirk_name = NULL;
	g_autofree gchar *physical_id = NULL;
	g_autofree gchar *instance_id = NULL;

	if (!FU_DEVICE_CLASS (fu_realtek_mst_device_parent_class)->probe (device, error))
		return FALSE;

	physical_id = g_strdup_printf ("I2C_PATH=%s",
				       fu_udev_device_get_sysfs_path (
					       FU_UDEV_DEVICE (device)));
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

	if ((self->bus_device = fu_realtek_mst_device_locate_bus (self, error)) == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_realtek_mst_device_open (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	const gchar *bus_path = fu_udev_device_get_device_file (self->bus_device);
	gint bus_fd;

	/* open the bus and not self */
	if ((bus_fd = g_open (bus_path, O_RDWR)) == -1) {
		g_set_error (error, G_IO_ERROR,
#ifdef HAVE_ERRNO_H
			     g_io_error_from_errno (errno),
#else
	       		     G_IO_ERROR_FAILED,
#endif
			     "failed to open %s", bus_path);

		return FALSE;
	}
	fu_udev_device_set_fd (FU_UDEV_DEVICE (self), bus_fd);
	fu_udev_device_set_flags (FU_UDEV_DEVICE (device),
				  FU_UDEV_DEVICE_FLAG_NONE);
	g_debug ("bus opened");

	return FU_DEVICE_CLASS (fu_realtek_mst_device_parent_class)->open (device, error);
}

static gboolean
fu_realtek_mst_device_setup (FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	gint bus_fd = fu_udev_device_get_fd (self);
	g_autofree gchar *version_str = NULL;
	guint8 active_bank;
	const guint8 enter_ddcci_mode[] = {0xca, 0x09};
	guint8 response[11];

	g_return_val_if_fail(bus_fd != -1, FALSE);

	/* set target address to device address */
	if (!fu_udev_device_ioctl (self, I2C_SLAVE, (guint8 *) 0x35, NULL, error))
		return FALSE;

	/* switch to DDCCI mode */
	if (!fu_udev_device_pwrite_full (self,
					 0, enter_ddcci_mode,
					 sizeof (enter_ddcci_mode),
					 error))
		return FALSE;
	/*if (write (bus_fd, enter_ddcci_mode, sizeof (enter_ddcci_mode)) !=
		sizeof (enter_ddcci_mode)) {
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
			     "failed to write mode switch command: %s",
			     g_strerror (errno));
		return FALSE;
	}*/

	/* wait for mode switch to complete */
	g_usleep (200 * G_TIME_SPAN_MILLISECOND);

	/* request dual bank state and read back */
	if (!fu_udev_device_pwrite (self, 0, 0x01, error))
		return FALSE;
	if (!fu_udev_device_pread_full (self, 0, response, sizeof (response), error))
		return FALSE;

	/* validate response to ensure device is updateable: it must be in
	 * dual-bank-diff firmware mode */
	if (response[0] != 0xca || response[1] != 9) {
		/* unexpected response code or length usually means the current
		 * firmware doesn't support dual-bank mode at all */
		g_debug ("not updatable with response code %#x, length %d",
			 response[0], response[1]);
		return TRUE;
	}
	if (response[2] != 1) {
		info->is_enabled = FALSE;
		return TRUE;
	}
	info->is_enabled = TRUE;

	info->mode = response[3];
	if (info->mode > DUAL_BANK_MAX_VALUE) {
		g_debug ("unexpected dual bank mode value %#x", info->mode);
		info->is_enabled = FALSE;
		return TRUE;
	}

	info->active_bank = response[4];
	if (info->active_bank > FLASH_BANK_MAX_VALUE) {
		g_debug ("unexpected active flash bank value %#x",
			 info->active_bank);
		info->is_enabled = FALSE;
		return TRUE;
	}

	info->user1_version[0] = response[5];
	info->user1_version[1] = response[6];
	info->user2_version[0] = response[7];
	info->user2_version[1] = response[8];
	/* last two bytes of response are reserved */
	return TRUE;
}

static gboolean
fu_realtek_mst_device_probe_version (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	struct dual_bank_info info;
	guint8 *active_version;
	g_autofree gchar *version_str = NULL;

	/* ensure probed state is cleared in case of error */
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	self->active_bank = FLASH_BANK_INVALID;
	fu_device_set_version (device, NULL);

	if (!fu_realtek_mst_device_get_dual_bank_info (FU_REALTEK_MST_DEVICE (self),
						       &info, error))
		return FALSE;

	if (!info.is_enabled) {
		g_debug ("dual-bank mode is not enabled");
		return TRUE;
	}
	if (response[3] != 1) {
		g_debug ("dual-bank mode must be 1, was %d", response[3]);
		return TRUE;
	}
	/* dual-bank mode seems to be fully supported, so we can update
	 * regardless of the active bank- if it's FLASH_BANK_BOOT, updating is
	 * possible even if the current version is unknown */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* TODO we'll need this value when programming */
	active_bank = response[4];
	g_debug ("device is currently running from bank %d", response[4]);
	/* only user bank versions are reported, can't tell otherwise */
	if (active_bank < 1 || active_bank > 2)
		return TRUE;

	version_str = g_strdup_printf ("%d.%d",
				       response[4 + active_bank],
				       response[5 + active_bank]);
	fu_device_set_version (FU_DEVICE (self), version_str);

	return TRUE;
}

static void
fu_realtek_mst_device_init (FuRealtekMstDevice *self)
{
	FuDevice *device = FU_DEVICE (self);
	self->dp_aux_dev_name = NULL;
	self->bus_device = NULL;

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
	if (self->bus_device != NULL)
		g_object_unref (self->bus_device);

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
	klass_device->open = fu_realtek_mst_device_open;
	klass_device->setup = fu_realtek_mst_device_setup;
	/*
	klass_device->write_firmware = fu_flashrom_lspcon_i2c_spi_device_write_firmware;
	 */
}
