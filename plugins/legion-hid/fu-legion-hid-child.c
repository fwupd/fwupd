/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid-child.h"
#include "fu-legion-hid-device.h"
#include "fu-legion-hid-firmware.h"

struct _FuLegionHidChild {
	FuDevice parent_instance;
	FuLegionHidDeviceId id;
};

G_DEFINE_TYPE(FuLegionHidChild, fu_legion_hid_child, FU_TYPE_DEVICE)

static gboolean
fu_legion_hid_child_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuLegionHidChild *self = FU_LEGION_HID_CHILD(device);
	FuLegionHidDevice *proxy;
	guint32 version = 0;
	g_autoptr(FuFirmware) img = NULL;

	img = fu_firmware_get_image_by_id(firmware, fu_device_get_logical_id(device), error);
	if (img == NULL)
		return FALSE;
	proxy = FU_LEGION_HID_DEVICE(fu_device_get_proxy(device, error));
	if (proxy == NULL)
		return FALSE;
	if (!fu_legion_hid_device_execute_upgrade(proxy, img, error)) {
		g_prefix_error(error, "execute %s failed: ", fu_device_get_logical_id(device));
		return FALSE;
	}
	/*
	 * If only the controller is updated, the MCU will not restart,
	 * so the version number needs to be reset.If the version is not reset,
	 * fwupd will report an update failure.
	 */
	if (!fu_legion_hid_device_get_version(proxy, self->id, &version, error))
		return FALSE;
	fu_device_set_version_raw(device, version);

	/* success */
	return TRUE;
}

static gchar *
fu_legion_hid_child_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%X", (guint)version_raw);
}

static void
fu_legion_hid_child_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_legion_hid_child_setup(FuDevice *device, GError **error)
{
	FuLegionHidChild *self = FU_LEGION_HID_CHILD(device);
	FuLegionHidDevice *proxy;
	guint32 version = 0;

	proxy = FU_LEGION_HID_DEVICE(fu_device_get_proxy(device, error));
	if (proxy == NULL)
		return FALSE;
	if (!fu_legion_hid_device_get_version(proxy, self->id, &version, error))
		return FALSE;
	fu_device_set_version_raw(device, version);

	fu_device_add_instance_str(device, "CHILD", fu_device_get_logical_id(device));
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "CHILD", NULL))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_legion_hid_child_class_init(FuLegionHidChildClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_hid_child_setup;
	device_class->write_firmware = fu_legion_hid_child_write_firmware;
	device_class->set_progress = fu_legion_hid_child_set_progress;
	device_class->convert_version = fu_legion_hid_child_convert_version;
}

static void
fu_legion_hid_child_init(FuLegionHidChild *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-hid");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LEGION_HID_FIRMWARE);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_LEGION_HID_DEVICE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
}

FuLegionHidChild *
fu_legion_hid_child_new(FuDevice *parent, FuLegionHidDeviceId id)
{
	FuLegionHidChild *self =
	    g_object_new(FU_TYPE_LEGION_HID_CHILD, "proxy", parent, "parent", parent, NULL);
	self->id = id;
	fu_device_incorporate(FU_DEVICE(self),
			      parent,
			      FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID);
	return self;
}
