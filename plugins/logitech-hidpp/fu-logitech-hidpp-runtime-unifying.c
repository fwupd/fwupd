/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-hidpp.h"
#include "fu-logitech-hidpp-runtime-unifying.h"
#include "fu-logitech-hidpp-struct.h"

struct _FuLogitechHidppRuntimeUnifying {
	FuLogitechHidppRuntime parent_instance;
};

G_DEFINE_TYPE(FuLogitechHidppRuntimeUnifying,
	      fu_logitech_hidpp_runtime_unifying,
	      FU_TYPE_HIDPP_RUNTIME)
#define GET_PRIVATE(o) (fu_logitech_hidpp_runtime_unifying_get_instance_private(o))

static gboolean
fu_logitech_hidpp_runtime_unifying_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();
	g_autoptr(GError) error_local = NULL;

	msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
	msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
	msg->sub_id = FU_LOGITECH_HIDPP_SUBID_SET_REGISTER;
	msg->function_id = FU_LOGITECH_HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE;
	msg->data[0] = 'I';
	msg->data[1] = 'C';
	msg->data[2] = 'P';
	msg->hidpp_version = 1;
	msg->flags = FU_LOGITECH_HIDPP_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!fu_logitech_hidpp_send(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)),
				    msg,
				    FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS,
				    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_WRITE)) {
			g_debug("failed to detach to bootloader: %s", error_local->message);
		} else {
			g_prefix_error(&error_local, "failed to detach to bootloader: ");
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_unifying_setup_internal(FuDevice *device, GError **error)
{
	FuLogitechHidppRuntime *self = FU_HIDPP_RUNTIME(device);
	guint8 config[10];
	g_autofree gchar *version_fw = NULL;

	/* read all 10 bytes of the version register */
	memset(config, 0x00, sizeof(config));
	for (guint i = 0x01; i < 0x05; i++) {
		g_autoptr(FuLogitechHidppHidppMsg) msg = fu_logitech_hidpp_msg_new();

		/* workaround a bug in the 12.01 firmware, which fails with
		 * INVALID_VALUE when reading MCU1_HW_VERSION */
		if (i == 0x03)
			continue;

		msg->report_id = FU_LOGITECH_HIDPP_REPORT_ID_SHORT;
		msg->device_id = FU_LOGITECH_HIDPP_DEVICE_IDX_RECEIVER;
		msg->sub_id = FU_LOGITECH_HIDPP_SUBID_GET_REGISTER;
		msg->function_id = FU_LOGITECH_HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION;
		msg->data[0] = i;
		msg->hidpp_version = 1;
		if (!fu_logitech_hidpp_transfer(fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self)),
						msg,
						error)) {
			g_prefix_error(error, "failed to read device config: ");
			return FALSE;
		}
		if (!fu_memcpy_safe(config,
				    sizeof(config),
				    i * 2, /* dst */
				    msg->data,
				    sizeof(msg->data),
				    0x1, /* src */
				    2,
				    error))
			return FALSE;
	}

	/* get firmware version */
	version_fw = fu_logitech_hidpp_format_version("RQR",
						      config[2],
						      config[3],
						      (guint16)config[4] << 8 | config[5]);
	fu_device_set_version(device, version_fw);

	/* get bootloader version */
	if (fu_logitech_hidpp_runtime_get_version_bl_major(self) > 0) {
		g_autofree gchar *version_bl = NULL;
		version_bl = fu_logitech_hidpp_format_version(
		    "BOT",
		    fu_logitech_hidpp_runtime_get_version_bl_major(self),
		    config[8],
		    config[9]);
		fu_device_set_version_bootloader(FU_DEVICE(device), version_bl);

		/* is the USB receiver expecting signed firmware */
		if ((fu_logitech_hidpp_runtime_get_version_bl_major(self) == 0x01 &&
		     config[8] >= 0x04) ||
		    (fu_logitech_hidpp_runtime_get_version_bl_major(self) == 0x03 &&
		     config[8] >= 0x02)) {
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
			fu_device_add_protocol(device, "com.logitech.unifyingsigned");
		}
	}
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		fu_device_add_protocol(device, "com.logitech.unifying");
	}

	/* enable HID++ notifications */
	if (!fu_logitech_hidpp_runtime_enable_notifications(self, error)) {
		g_prefix_error(error, "failed to enable notifications: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_unifying_setup(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	for (guint i = 0; i < 5; i++) {
		g_clear_error(&error_local);
		/* HID++1.0 devices have to sleep to allow Solaar to talk to
		 * the device first -- we can't use the SwID as this is a
		 * HID++2.0 feature */
		fu_device_sleep(device, 200); /* ms */
		if (fu_logitech_hidpp_runtime_unifying_setup_internal(device, &error_local))
			return TRUE;
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	g_propagate_error(error, g_steal_pointer(&error_local));
	return FALSE;
}

static void
fu_logitech_hidpp_runtime_unifying_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 27, "reload");
}

static void
fu_logitech_hidpp_runtime_unifying_class_init(FuLogitechHidppRuntimeUnifyingClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->detach = fu_logitech_hidpp_runtime_unifying_detach;
	device_class->setup = fu_logitech_hidpp_runtime_unifying_setup;
	device_class->set_progress = fu_logitech_hidpp_runtime_unifying_set_progress;
}

static void
fu_logitech_hidpp_runtime_unifying_init(FuLogitechHidppRuntimeUnifying *self)
{
}
