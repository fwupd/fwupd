/*
 * Copyright 2018 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock-common.h"

struct _FuDellDockHub {
	FuHidDevice parent_instance;
	guint8 unlock_target;
	guint64 blob_major_offset;
	guint64 blob_minor_offset;
};

G_DEFINE_TYPE(FuDellDockHub, fu_dell_dock_hub, FU_TYPE_HID_DEVICE)

void
fu_dell_dock_hub_add_instance(FuDevice *device, guint8 dock_type)
{
	g_autofree gchar *devid = NULL;

	if (dock_type == DOCK_BASE_TYPE_ATOMIC) {
		devid = g_strdup_printf("USB\\VID_%04X&PID_%04X&atomic_hub",
					(guint)fu_device_get_vid(device),
					(guint)fu_device_get_pid(device));
	} else {
		devid = g_strdup_printf("USB\\VID_%04X&PID_%04X&hub",
					(guint)fu_device_get_vid(device),
					(guint)fu_device_get_pid(device));
	}
	fu_device_add_instance_id(device, devid);
}

static gboolean
fu_dell_dock_hub_probe(FuDevice *device, GError **error)
{
	fu_device_set_logical_id(device, "hub");
	fu_device_add_protocol(device, "com.dell.dock");

	return TRUE;
}

static gboolean
fu_dell_dock_hub_write_fw(FuDevice *device,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDellDockHub *self = FU_DELL_DOCK_HUB(device);
	gsize fw_size = 0;
	const guint8 *data;
	gsize write_size;
	gsize nwritten = 0;
	guint32 address = 0;
	gboolean result = FALSE;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 49, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 50, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data(fw, &fw_size);
	write_size = (fw_size / HIDI2C_MAX_WRITE) >= 1 ? HIDI2C_MAX_WRITE : fw_size;

	dynamic_version = g_strdup_printf("%02x.%02x",
					  data[self->blob_major_offset],
					  data[self->blob_minor_offset]);
	g_info("writing hub firmware version %s", dynamic_version);

	if (!fu_dell_dock_set_power(device, self->unlock_target, TRUE, error))
		return FALSE;

	if (!fu_dell_dock_hid_raise_mcu_clock(device, TRUE, error))
		return FALSE;

	/* erase */
	if (!fu_dell_dock_hid_erase_bank(device, 1, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	do {
		/* last packet */
		if (fw_size - nwritten < write_size)
			write_size = fw_size - nwritten;

		if (!fu_dell_dock_hid_write_flash(device, address, data, write_size, error))
			return FALSE;
		nwritten += write_size;
		data += write_size;
		address += write_size;
		fu_progress_set_percentage_full(fu_progress_get_child(progress), nwritten, fw_size);
	} while (nwritten < fw_size);
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_dell_dock_hid_verify_update(device, &result, error))
		return FALSE;
	if (!result) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Failed to verify the update");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_version(device, dynamic_version);
	return TRUE;
}

static gboolean
fu_dell_dock_hub_setup(FuDevice *device, GError **error)
{
	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dell_dock_hub_parent_class)->setup(device, error))
		return FALSE;

	/* skip version setup here as we don't know HID header format yet */
	if (fu_device_has_private_flag(device, FU_DELL_DOCK_HUB_FLAG_HAS_BRIDGE))
		return TRUE;

	return fu_dell_dock_hid_get_hub_version(device, error);
}

static gboolean
fu_dell_dock_hub_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuDellDockHub *self = FU_DELL_DOCK_HUB(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "DellDockUnlockTarget") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->unlock_target = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockBlobMajorOffset") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->blob_major_offset = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockBlobMinorOffset") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->blob_minor_offset = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_dell_dock_hub_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_dell_dock_hub_parent_class)->finalize(object);
}

static void
fu_dell_dock_hub_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_dock_hub_init(FuDellDockHub *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_device_register_private_flag(FU_DEVICE(self), FU_DELL_DOCK_HUB_FLAG_HAS_BRIDGE);
}

static void
fu_dell_dock_hub_class_init(FuDellDockHubClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_dock_hub_finalize;
	device_class->setup = fu_dell_dock_hub_setup;
	device_class->probe = fu_dell_dock_hub_probe;
	device_class->write_firmware = fu_dell_dock_hub_write_fw;
	device_class->set_quirk_kv = fu_dell_dock_hub_set_quirk_kv;
	device_class->set_progress = fu_dell_dock_hub_set_progress;
}

FuDellDockHub *
fu_dell_dock_hub_new(FuUsbDevice *device)
{
	FuDellDockHub *self = g_object_new(FU_TYPE_DELL_DOCK_HUB, NULL);
	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device), FU_DEVICE_INCORPORATE_FLAG_ALL);
	return self;
}
