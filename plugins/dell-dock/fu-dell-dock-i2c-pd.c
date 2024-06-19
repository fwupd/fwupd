/*
 * Copyright 2024 Dell Inc.
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

struct _FuDellDockPd {
	FuDevice parent_instance;
	guint64 blob_version_offset;
	guint8 pd_subtype;
	guint8 pd_instance;
	guint8 pd_identifier;
};

G_DEFINE_TYPE(FuDellDockPd, fu_dell_dock_pd, FU_TYPE_DEVICE)

static gchar *
fu_dell_dock_pd_ver_string(guint32 pd_version)
{
	/* guint32 BCD */
	return g_strdup_printf("%02x.%02x.%02x.%02x",
			       (pd_version >> 0) & 0xff,
			       (pd_version >> 8) & 0xff,
			       (pd_version >> 16) & 0xff,
			       (pd_version >> 24) & 0xff);
}

static gboolean
fu_dell_dock_pd_setup(FuDevice *device, GError **error)
{
	FuDellDockPd *self = FU_DELL_DOCK_PD(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autofree gchar *dynamic_version = NULL;
	g_autofree gchar *logical_id = NULL;
	DockBaseType dock_type;
	guint8 dock_sku;
	g_autofree const gchar *devname = NULL;
	g_autofree const gchar *guid = NULL;
	guint32 pd_version;
	guint8 devtype = EC_V2_DOCK_DEVICE_TYPE_PD;

	/* name */
	devname = g_strdup(
	    fu_dell_dock_ec_v2_devicetype_to_str(devtype, self->pd_subtype, self->pd_instance));
	fu_device_set_name(device, devname);

	/* IDs */
	dock_type = fu_dell_dock_ec_v2_get_dock_type(proxy);
	dock_sku = fu_dell_dock_ec_v2_get_dock_sku(proxy);

	logical_id = g_strdup_printf("PD\\DOCKTYPE_%02x&DOCKSKU_%02x&INSTANCE_%02x",
				     dock_type,
				     dock_sku,
				     self->pd_instance);
	fu_device_set_logical_id(device, logical_id);
	fu_device_add_instance_id(device, logical_id);

	guid = fwupd_guid_hash_string(logical_id);
	fu_device_add_guid(device, guid);

	/* relationship */
	fu_device_add_child(proxy, device);

	/* version */
	pd_version = fu_dell_dock_ec_v2_get_pd_version(proxy, self->pd_subtype, self->pd_instance);
	dynamic_version = fu_dell_dock_pd_ver_string(pd_version);
	fu_device_set_version(device, dynamic_version);

	return TRUE;
}

static gboolean
fu_dell_dock_pd_write(FuDevice *device,
		      FuFirmware *firmware,
		      FuProgress *progress,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	FuDellDockPd *self = FU_DELL_DOCK_PD(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_whdr = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GByteArray) res = g_byte_array_sized_new(HID_v2_RESPONSE_LENGTH);

	/* basic tests */
	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default firmware image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* construct writing buffer */
	fw_whdr =
	    fu_dell_dock_hid_v2_fwup_pkg_new(fw, EC_V2_DOCK_DEVICE_TYPE_PD, self->pd_identifier);

	/* prepare the chunks */
	chunks = fu_chunk_array_new_from_bytes(fw_whdr, 0, HID_v2_DATA_PAGE_SZ);

	/* write to device */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_dell_dock_hid_v2_write(proxy, fu_chunk_get_bytes(chk), error))
			return FALSE;
	}

	/* verify response
	fu_byte_array_set_size(res, HID_v2_RESPONSE_LENGTH, 0xff);
	if (!fu_dell_dock_hid_v2_read(proxy, res, error))
		return FALSE;

	if (res->data[1] != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed verification on HID response[1], expected 0x00, got 0x%02x",
			    res->data[1]);
		return FALSE;
	}
	*/

	/* dock will reboot to re-read; this is to appease the daemon */
	g_debug("pd firmware written successfully; waiting for dock to reboot");
	return TRUE;
}

static gboolean
fu_dell_dock_pd_open(FuDevice *device, GError **error)
{
	return fu_device_open(fu_device_get_proxy(device), error);
}

static gboolean
fu_dell_dock_pd_close(FuDevice *device, GError **error)
{
	return fu_device_close(fu_device_get_proxy(device), error);
}

static void
fu_dell_dock_pd_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_dell_dock_pd_parent_class)->finalize(object);
}

static void
fu_dell_dock_pd_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 9, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_dell_dock_pd_init(FuDellDockPd *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.dock");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SKIPS_RESTART);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
}

static void
fu_dell_dock_pd_class_init(FuDellDockPdClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_dock_pd_finalize;
	device_class->write_firmware = fu_dell_dock_pd_write;
	device_class->open = fu_dell_dock_pd_open;
	device_class->setup = fu_dell_dock_pd_setup;
	device_class->close = fu_dell_dock_pd_close;
	device_class->set_progress = fu_dell_dock_pd_set_progress;
}

FuDellDockPd *
fu_dell_dock_pd_new(FuDevice *proxy, guint8 subtype, guint8 instance)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellDockPd *self = NULL;
	self = g_object_new(FU_TYPE_DELL_DOCK_PD, "context", ctx, NULL);
	self->pd_subtype = subtype;
	self->pd_instance = instance;
	self->pd_identifier = instance + 1;
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	return self;
}
