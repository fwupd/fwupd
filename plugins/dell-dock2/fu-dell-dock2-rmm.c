/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-dock2-common.h"

struct _FuDellDock2Rmm {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuDellDock2Rmm, fu_dell_dock2_rmm, FU_TYPE_DEVICE)

static gchar *
fu_dell_dock2_rmm_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%u.%u.%u",
			       (guint)(version_raw >> 16) & 0xFF,
			       (guint)(version_raw >> 24) & 0xFF,
			       (guint)(version_raw >> 8) & 0xFF);
}

static gboolean
fu_dell_dock2_rmm_setup(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autofree const gchar *devname = NULL;
	guint32 rmm_version;
	FuDellDock2BaseType dock_type = fu_dell_dock2_ec_get_dock_type(proxy);
	guint8 dev_type = DELL_DOCK2_EC_DEV_TYPE_RMM;

	/* name */
	devname = g_strdup_printf("%s", fu_dell_dock2_ec_devicetype_to_str(dev_type, 0, 0));
	fu_device_set_name(device, devname);
	fu_device_set_logical_id(device, devname);

	/* IDs */
	fu_device_add_instance_u8(device, "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(device, "DEVTYPE", dev_type);
	fu_device_build_instance_id(device, error, "EC", "DOCKTYPE", "DEVTYPE", NULL);

	/* version */
	rmm_version = fu_dell_dock2_ec_get_rmm_version(proxy);
	fu_device_set_version_raw(device, GUINT32_FROM_BE(rmm_version));

	return TRUE;
}

static gboolean
fu_dell_dock2_rmm_write(FuDevice *device,
			FuFirmware *firmware,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_whdr = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);

	/* basic tests */
	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default firmware image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* construct writing buffer */
	fw_whdr = fu_dell_dock2_hid_fwup_pkg_new(fw, DELL_DOCK2_EC_DEV_TYPE_RMM, 0);

	/* prepare the chunks */
	chunks = fu_chunk_array_new_from_bytes(fw_whdr, 0, DELL_DOCK2_EC_HID_DATA_PAGE_SZ);

	/* write to device */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_dell_dock2_ec_hid_write(proxy, fu_chunk_get_bytes(chk), error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(progress,
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}

	/* dock will reboot to re-read; this is to appease the daemon */
	g_debug("Remote Management firmware written successfully");
	return TRUE;
}

static void
fu_dell_dock2_rmm_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 9, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_dell_dock2_rmm_init(FuDellDock2Rmm *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.dock2");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_icon(FU_DEVICE(self), "dock-usb");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_dell_dock2_rmm_class_init(FuDellDock2RmmClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_dock2_rmm_write;
	device_class->setup = fu_dell_dock2_rmm_setup;
	device_class->set_progress = fu_dell_dock2_rmm_set_progress;
	device_class->convert_version = fu_dell_dock2_rmm_convert_version;
}

FuDellDock2Rmm *
fu_dell_dock2_rmm_new(FuDevice *proxy)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellDock2Rmm *self = NULL;
	self = g_object_new(FU_TYPE_DELL_DOCK2_RMM, "context", ctx, NULL);
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	return self;
}
