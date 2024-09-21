/*
 * Copyright 2019 Intel Corporation.
 * Copyright 2019 Dell Inc.
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

#include <string.h>

#include "fu-dell-dock-common.h"

#define I2C_TBT_ADDRESS 0xa2

const FuHIDI2CParameters tbt_base_settings = {
    .i2ctargetaddr = I2C_TBT_ADDRESS,
    .regaddrlen = 1,
    .i2cspeed = I2C_SPEED_400K,
};

/* TR Device ID */
#define PID_OFFSET 0x05
#define INTEL_PID  0x15ef

/* earlier versions have bugs */
#define MIN_NVM "36.01"

struct _FuDellDockTbt {
	FuDevice parent_instance;
	guint8 unlock_target;
	guint64 blob_major_offset;
	guint64 blob_minor_offset;
	gchar *hub_minimum_version;
};

G_DEFINE_TYPE(FuDellDockTbt, fu_dell_dock_tbt, FU_TYPE_DEVICE)

static gboolean
fu_dell_dock_tbt_write_fw(FuDevice *device,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT(device);
	guint32 start_offset = 0;
	gsize image_size = 0;
	const guint8 *buffer;
	guint16 target_system = 0;
	g_autoptr(GTimer) timer = g_timer_new();
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	buffer = g_bytes_get_data(fw, &image_size);

	dynamic_version = g_strdup_printf("%02x.%02x",
					  buffer[self->blob_major_offset],
					  buffer[self->blob_minor_offset]);
	g_info("writing Thunderbolt firmware version %s", dynamic_version);
	g_debug("Total Image size: %" G_GSIZE_FORMAT, image_size);

	memcpy(&start_offset, buffer, sizeof(guint32)); /* nocheck:blocked */
	g_debug("Header size 0x%x", start_offset);
	if (start_offset > image_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Image header is too big (0x%x)",
			    start_offset);
		return FALSE;
	}

	memcpy(&target_system, /* nocheck:blocked */
	       buffer + start_offset + PID_OFFSET,
	       sizeof(guint16));
	if (target_system != INTEL_PID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Image is not intended for this system (0x%x)",
			    target_system);
		return FALSE;
	}

	buffer += start_offset;
	image_size -= start_offset;

	g_debug("waking Thunderbolt controller");
	if (!fu_dell_dock_hid_tbt_wake(fu_device_get_proxy(device), &tbt_base_settings, error))
		return FALSE;
	fu_device_sleep(device, 2000);

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < image_size; i += HIDI2C_MAX_WRITE, buffer += HIDI2C_MAX_WRITE) {
		guint8 write_size = (image_size - i) > HIDI2C_MAX_WRITE ? HIDI2C_MAX_WRITE
									: (image_size - i);

		if (!fu_dell_dock_hid_tbt_write(fu_device_get_proxy(device),
						i,
						buffer,
						write_size,
						&tbt_base_settings,
						error))
			return FALSE;

		fu_progress_set_percentage_full(progress, i, image_size);
	}
	g_debug("writing took %f seconds", g_timer_elapsed(timer, NULL));

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);

	if (fu_dell_dock_ec_tbt_passive(fu_device_get_parent(device))) {
		g_info("using passive flow for Thunderbolt");
	} else if (!fu_dell_dock_hid_tbt_authenticate(fu_device_get_proxy(device),
						      &tbt_base_settings,
						      error)) {
		g_prefix_error(error, "failed to authenticate: ");
		return FALSE;
	}

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_version(device, dynamic_version);

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "DellDockUnlockTarget") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->unlock_target = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockInstallDurationI2C") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 60 * 60 * 24, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_device_set_install_duration(device, tmp);
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockHubVersionLowest") == 0) {
		self->hub_minimum_version = g_strdup(value);
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

static gboolean
fu_dell_dock_tbt_setup(FuDevice *device, GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT(device);
	FuDevice *parent;
	const gchar *version;
	const gchar *hub_version;

	/* set version from EC if we know it */
	parent = fu_device_get_parent(device);
	version = fu_dell_dock_ec_get_tbt_version(parent);
	if (version != NULL) {
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PAIR);
		fu_device_set_version(device, version);
	}

	/* minimum version of NVM that supports this feature */
	if (version == NULL ||
	    fu_version_compare(version, MIN_NVM, FWUPD_VERSION_FORMAT_PAIR) < 0) {
		fu_device_set_update_error(
		    device,
		    "Updates over I2C are disabled due to insufficient NVM version");
		return TRUE;
	}
	/* minimum Hub2 version that supports this feature */
	hub_version = fu_device_get_version(fu_device_get_proxy(device));
	if (fu_version_compare(hub_version, self->hub_minimum_version, FWUPD_VERSION_FORMAT_PAIR) <
	    0) {
		fu_device_set_update_error(
		    device,
		    "Updates over I2C are disabled due to insufficient USB 3.1 G2 hub version");
		return TRUE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_probe(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	fu_device_incorporate(device, parent, FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	fu_device_set_logical_id(FU_DEVICE(device), "tbt");
	fu_device_add_instance_id(device, DELL_DOCK_TBT_INSTANCE_ID);
	/* this is true only when connected to non-thunderbolt port */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_open(FuDevice *device, GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT(device);

	g_return_val_if_fail(self->unlock_target != 0, FALSE);

	if (!fu_device_open(fu_device_get_proxy(device), error))
		return FALSE;

	/* adjust to access controller */
	if (!fu_dell_dock_set_power(device, self->unlock_target, TRUE, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_tbt_close(FuDevice *device, GError **error)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT(device);

	/* adjust to access controller */
	if (!fu_dell_dock_set_power(device, self->unlock_target, FALSE, error))
		return FALSE;

	return fu_device_close(fu_device_get_proxy(device), error);
}

static void
fu_dell_dock_tbt_finalize(GObject *object)
{
	FuDellDockTbt *self = FU_DELL_DOCK_TBT(object);
	g_free(self->hub_minimum_version);

	G_OBJECT_CLASS(fu_dell_dock_tbt_parent_class)->finalize(object);
}

static void
fu_dell_dock_tbt_init(FuDellDockTbt *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.thunderbolt");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
}

static void
fu_dell_dock_tbt_class_init(FuDellDockTbtClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_dock_tbt_finalize;
	device_class->probe = fu_dell_dock_tbt_probe;
	device_class->setup = fu_dell_dock_tbt_setup;
	device_class->open = fu_dell_dock_tbt_open;
	device_class->close = fu_dell_dock_tbt_close;
	device_class->write_firmware = fu_dell_dock_tbt_write_fw;
	device_class->set_quirk_kv = fu_dell_dock_tbt_set_quirk_kv;
}

FuDellDockTbt *
fu_dell_dock_tbt_new(FuDevice *proxy)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellDockTbt *self = g_object_new(FU_TYPE_DELL_DOCK_TBT, "context", ctx, NULL);
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	return self;
}
