/*
 * Copyright (C) 2019 Intel Corporation.
 * Copyright (C) 2019 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <string.h>

#include "fwupd-error.h"
#include "fu-device.h"
#include "fu-common.h"
#include "fu-common-version.h"

#include "fu-dell-dock-common.h"

#define I2C_TBT_ADDRESS 0xa2

const FuHIDI2CParameters tbt_base_settings = {
	.i2cslaveaddr = I2C_TBT_ADDRESS,
	.regaddrlen = 1,
	.i2cspeed = I2C_SPEED_400K,
};

/* TR Device ID */
#define PID_OFFSET		0x05
#define INTEL_PID		0x15ef

/* earlier versions have bugs */
#define MIN_NVM			"36.01"

struct _FuDellDockTbt {
	FuDevice			 parent_instance;
	FuDevice 			*symbiote;
	guint8				 unlock_target;
	guint64				 blob_major_offset;
	guint64				 blob_minor_offset;
	gchar				*hub_minimum_version;
};

G_DEFINE_TYPE (FuDellDockTbt, fu_dell_dock_tbt, FU_TYPE_DEVICE)

static gboolean
fu_dell_dock_tbt_write_fw (FuDevice *device,
			   FuFirmware *firmware,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT (device);
	guint32 start_offset = 0;
	gsize image_size = 0;
	const guint8 *buffer;
	guint16 target_system = 0;
	g_autoptr(GTimer) timer = g_timer_new ();
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (FU_IS_FIRMWARE (firmware), FALSE);

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	buffer = g_bytes_get_data (fw, &image_size);

	dynamic_version = g_strdup_printf ("%02x.%02x",
					   buffer[self->blob_major_offset],
					   buffer[self->blob_minor_offset]);
	g_debug ("writing Thunderbolt firmware version %s", dynamic_version);
	g_debug ("Total Image size: %" G_GSIZE_FORMAT, image_size);

	memcpy (&start_offset, buffer, sizeof (guint32));
	g_debug ("Header size 0x%x", start_offset);
	if (start_offset > image_size) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
			     "Image header is too big (0x%x)",
			     start_offset);
		return FALSE;
	}

	memcpy (&target_system, buffer + start_offset + PID_OFFSET, sizeof (guint16));
	if (target_system != INTEL_PID) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
			     "Image is not intended for this system (0x%x)",
			     target_system);
		return FALSE;
	}

	buffer += start_offset;
	image_size -= start_offset;

	g_debug ("waking Thunderbolt controller");
	if (!fu_dell_dock_hid_tbt_wake (self->symbiote, &tbt_base_settings, error))
		return FALSE;
	g_usleep (2000000);

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < image_size; i+= HIDI2C_MAX_WRITE, buffer += HIDI2C_MAX_WRITE) {
		guint8 write_size = (image_size - i) > HIDI2C_MAX_WRITE ?
				    HIDI2C_MAX_WRITE : (image_size - i);

		if (!fu_dell_dock_hid_tbt_write (self->symbiote,
						 i,
						 buffer,
						 write_size,
						 &tbt_base_settings,
						 error))
			return FALSE;

		fu_device_set_progress_full (device, i, image_size);
	}
	g_debug ("writing took %f seconds",
		 g_timer_elapsed (timer, NULL));

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);

	if (fu_dell_dock_ec_tbt_passive (fu_device_get_parent (device))) {
		g_debug ("using passive flow for Thunderbolt");
	} else if (!fu_dell_dock_hid_tbt_authenticate (self->symbiote,
						       &tbt_base_settings,
						       error)) {
		g_prefix_error (error, "failed to authenticate: ");
		return FALSE;
	}

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_version (device, dynamic_version, FWUPD_VERSION_FORMAT_PAIR);

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_set_quirk_kv (FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT (device);

	if (g_strcmp0 (key, "DellDockUnlockTarget") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT8) {
			self->unlock_target = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DellDockUnlockTarget");
		return FALSE;
	} else if (g_strcmp0 (key, "DellDockInstallDurationI2C") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		fu_device_set_install_duration (device, tmp);
		return TRUE;
	} else if (g_strcmp0 (key, "DellDockHubVersionLowest") == 0) {
		self->hub_minimum_version = g_strdup (value);
		return TRUE;
	} else if (g_strcmp0 (key, "DellDockBlobMajorOffset") == 0) {
		self->blob_major_offset = fu_common_strtoull (value);
		return TRUE;
	} else if (g_strcmp0 (key, "DellDockBlobMinorOffset") == 0) {
		self->blob_minor_offset = fu_common_strtoull (value);
		return TRUE;
	}
	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}


static gboolean
fu_dell_dock_tbt_setup (FuDevice *device, GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT (device);
	FuDevice *parent;
	const gchar *version;
	const gchar *hub_version;

	/* set version from EC if we know it */
	parent = fu_device_get_parent (device);
	version = fu_dell_dock_ec_get_tbt_version (parent);
	if (version != NULL)
		fu_device_set_version (device, version, FWUPD_VERSION_FORMAT_PAIR);

	/* minimum version of NVM that supports this feature */
	if (version == NULL ||
	    fu_common_vercmp_full (version, MIN_NVM, FWUPD_VERSION_FORMAT_PAIR) < 0) {
		fu_device_set_update_error (device,
					   "Updates over I2C are disabled due to insuffient NVM version");
		return TRUE;
	}
	/* minimum Hub2 version that supports this feature */
	hub_version = fu_device_get_version (self->symbiote);
	if (fu_common_vercmp_full (hub_version,
				   self->hub_minimum_version,
				   FWUPD_VERSION_FORMAT_PAIR) < 0) {
		fu_device_set_update_error (device,
					   "Updates over I2C are disabled due to insufficient USB 3.1 G2 hub version");
		return TRUE;
	}

	fu_dell_dock_clone_updatable (device);

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_probe (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	fu_device_set_physical_id (device, fu_device_get_physical_id (parent));
	fu_device_set_logical_id (FU_DEVICE (device), "tbt");
	fu_device_add_instance_id (device, DELL_DOCK_TBT_INSTANCE_ID);
	/* this is true only when connected to non-thunderbolt port */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_open (FuDevice *device, GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT (device);
	FuDevice *parent;

	g_return_val_if_fail (self->unlock_target != 0, FALSE);

	parent = fu_device_get_parent (device);
	if (parent == NULL) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no parent");
		return FALSE;
	}

	if (self->symbiote == NULL)
		self->symbiote = g_object_ref (fu_dell_dock_ec_get_symbiote (parent));

	if (!fu_device_open (self->symbiote, error))
		return FALSE;

	/* adjust to access controller */
	if (!fu_dell_dock_set_power (device, self->unlock_target, TRUE, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_close (FuDevice *device, GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT (device);

	/* adjust to access controller */
	if (!fu_dell_dock_set_power (device, self->unlock_target, FALSE, error))
		return FALSE;

	return fu_device_close (self->symbiote, error);
}

static void
fu_dell_dock_tbt_finalize (GObject *object)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT (object);
	g_object_unref (self->symbiote);
	g_free (self->hub_minimum_version);

	G_OBJECT_CLASS (fu_dell_dock_tbt_parent_class)->finalize (object);
}

static void
fu_dell_dock_tbt_init (FuDellDockTbt *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.intel.thunderbolt");
}

static void
fu_dell_dock_tbt_class_init (FuDellDockTbtClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_dell_dock_tbt_finalize;
	klass_device->probe = fu_dell_dock_tbt_probe;
	klass_device->setup = fu_dell_dock_tbt_setup;
	klass_device->open = fu_dell_dock_tbt_open;
	klass_device->close = fu_dell_dock_tbt_close;
	klass_device->write_firmware = fu_dell_dock_tbt_write_fw;
	klass_device->set_quirk_kv = fu_dell_dock_tbt_set_quirk_kv;
}

FuDellDockTbt *
fu_dell_dock_tbt_new (void)
{
	FuDellDockTbt *device = NULL;
	device = g_object_new (FU_TYPE_DELL_DOCK_TBT, NULL);
	return device;
}
