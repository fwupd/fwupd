/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-runtime-unifying.h"
#include "fu-logitech-hidpp-hidpp.h"

struct _FuLogitechHidPpRuntimeUnifying
{
	FuLogitechHidPpRuntime		 parent_instance;
};

G_DEFINE_TYPE (FuLogitechHidPpRuntimeUnifying, fu_logitech_hidpp_runtime_unifying, FU_TYPE_HIDPP_RUNTIME)
#define GET_PRIVATE(o) (fu_logitech_hidpp_runtime_unifying_get_instance_private (o))

static gboolean
fu_logitech_hidpp_runtime_unifying_detach (FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME (device);
	g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();
	g_autoptr(GError) error_local = NULL;

	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
	msg->sub_id = HIDPP_SUBID_SET_REGISTER;
	msg->function_id = HIDPP_REGISTER_DEVICE_FIRMWARE_UPDATE_MODE;
	msg->data[0] = 'I';
	msg->data[1] = 'C';
	msg->data[2] = 'P';
	msg->hidpp_version = 1;
	msg->flags = FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT;
	if (!fu_logitech_hidpp_send (fu_logitech_hidpp_runtime_get_io_channel (self),
				     msg,
				     FU_UNIFYING_DEVICE_TIMEOUT_MS,
				     &error_local)) {
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_WRITE)) {
			g_debug ("failed to detach to bootloader: %s", error_local->message);
		} else {
			g_prefix_error (&error_local, "failed to detach to bootloader: ");
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}


static gboolean
fu_logitech_hidpp_runtime_unifying_setup_internal (FuDevice *device, GError **error)
{
	FuLogitechHidPpRuntime *self = FU_HIDPP_RUNTIME (device);
	guint8 config[10];
	g_autofree gchar *version_fw = NULL;

	/* read all 10 bytes of the version register */
	memset (config, 0x00, sizeof (config));
	for (guint i = 0x01; i < 0x05; i++) {
		g_autoptr(FuLogitechHidPpHidppMsg) msg = fu_logitech_hidpp_msg_new ();

		/* workaround a bug in the 12.01 firmware, which fails with
		 * INVALID_VALUE when reading MCU1_HW_VERSION */
		if (i == 0x03)
			continue;

		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = HIDPP_DEVICE_ID_RECEIVER;
		msg->sub_id = HIDPP_SUBID_GET_REGISTER;
		msg->function_id = HIDPP_REGISTER_DEVICE_FIRMWARE_INFORMATION;
		msg->data[0] = i;
		msg->hidpp_version = 1;
		if (!fu_logitech_hidpp_transfer (fu_logitech_hidpp_runtime_get_io_channel (self), msg, error)) {
			g_prefix_error (error, "failed to read device config: ");
			return FALSE;
		}
		if (!fu_memcpy_safe (config, sizeof(config), i * 2,	/* dst */
				     msg->data, sizeof(msg->data), 0x1,	/* src */
				     2, error))
			return FALSE;
	}

	/* get firmware version */
	version_fw = fu_logitech_hidpp_format_version ("RQR",
						       config[2],
						       config[3],
						       (guint16) config[4] << 8 | config[5]);
	fu_device_set_version (device, version_fw);

	/* get bootloader version */
	if (fu_logitech_hidpp_runtime_get_version_bl_major (self) > 0) {
		g_autofree gchar *version_bl = NULL;
		version_bl = fu_logitech_hidpp_format_version ("BOT",
							       fu_logitech_hidpp_runtime_get_version_bl_major (self),
							       config[8],
							       config[9]);
		fu_device_set_version_bootloader (FU_DEVICE (device), version_bl);

		/* is the dongle expecting signed firmware */
		if ((fu_logitech_hidpp_runtime_get_version_bl_major (self) == 0x01 && config[8] >= 0x04) ||
		    (fu_logitech_hidpp_runtime_get_version_bl_major (self) == 0x03 && config[8] >= 0x02)) {
			fu_logitech_hidpp_runtime_set_signed_firmware (self, TRUE);
			fu_device_add_protocol (device, "com.logitech.unifyingsigned");
		}
	}
	if (!fu_logitech_hidpp_runtime_get_signed_firmware (self))
		fu_device_add_protocol (device, "com.logitech.unifying");

	/* enable HID++ notifications */
	if (!fu_logitech_hidpp_runtime_enable_notifications (self, error)) {
		g_prefix_error (error, "failed to enable notifications: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_runtime_unifying_setup (FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	for (guint i = 0; i < 5; i++) {
		g_clear_error (&error_local);
		/* HID++1.0 devices have to sleep to allow Solaar to talk to
		 * the device first -- we can't use the SwID as this is a
		 * HID++2.0 feature */
		g_usleep (200*1000);
		if (fu_logitech_hidpp_runtime_unifying_setup_internal (device, &error_local))
			return TRUE;
		if (!g_error_matches (error_local,
				      G_IO_ERROR,
				      G_IO_ERROR_INVALID_DATA)) {
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	}
	g_propagate_error (error, g_steal_pointer (&error_local));
	return FALSE;
}

static void
fu_logitech_hidpp_runtime_unifying_class_init (FuLogitechHidPpRuntimeUnifyingClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);

	klass_device->detach = fu_logitech_hidpp_runtime_unifying_detach;
	klass_device->setup = fu_logitech_hidpp_runtime_unifying_setup;
}

static void
fu_logitech_hidpp_runtime_unifying_init (FuLogitechHidPpRuntimeUnifying *self)
{
}
