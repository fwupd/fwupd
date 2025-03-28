/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUsbDeviceDs20"

#include "config.h"

#include "fu-bytes.h"
#include "fu-device-private.h"
#include "fu-input-stream.h"
#include "fu-string.h"
#include "fu-usb-device-fw-ds20.h"

struct _FuUsbDeviceFwDs20 {
	FuUsbDeviceDs20 parent_instance;
};

G_DEFINE_TYPE(FuUsbDeviceFwDs20, fu_usb_device_fw_ds20, FU_TYPE_USB_DEVICE_DS20)

#define DS20_VERSION_LOWEST ((1u << 16) | (8u << 8) | 5u)
#define DS20_VERSION_CURRENT                                                                       \
	((((guint32)FWUPD_MAJOR_VERSION) << 16) | (((guint32)FWUPD_MINOR_VERSION) << 8) |          \
	 ((guint)FWUPD_MICRO_VERSION))

static gboolean
fu_usb_device_fw_ds20_parse(FuUsbDeviceDs20 *self,
			    GInputStream *stream,
			    FuUsbDevice *device,
			    GError **error)
{
	gsize bufsz = 0;
	gsize bufsz_safe = 0;
	const guint8 *buf;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* convert to blob */
	blob = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
	if (blob == NULL)
		return FALSE;

	/* only accept Linux line-endings */
	buf = g_bytes_get_data(blob, &bufsz);
	if (g_strstr_len((const gchar *)buf, bufsz, "\r") != NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Windows line endings are not supported");
		return FALSE;
	}

	/* find the first NUL, if one exists */
	for (gsize i = 1; i < bufsz; i++) {
		if (buf[i] == '\0') {
			bufsz_safe = i - 1;
			break;
		}
	}

	/* no NUL is unexpected, but fine */
	if (bufsz_safe == 0)
		bufsz_safe = bufsz;

	if (!g_utf8_validate((const gchar *)buf, (gssize)bufsz_safe, NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "DS20 descriptor is not valid UTF-8");
		return FALSE;
	}

	/* add payload for ->export() */
	fu_firmware_set_bytes(FU_FIRMWARE(self), blob);

	/* split into lines */
	lines = fu_strsplit((const gchar *)buf, bufsz_safe, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		g_autofree gchar *key = NULL;
		g_autofree gchar *value = NULL;
		g_auto(GStrv) kv = NULL;

		if (lines[i][0] == '\0')
			continue;
		if (g_str_has_prefix(lines[i], "[") && g_str_has_suffix(lines[i], "]")) {
			g_debug("ignoring DS-20 group header: %s", lines[i]);
			continue;
		}
		kv = g_strsplit(lines[i], "=", 2);
		if (g_strv_length(kv) < 2) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "expected key=value for '%s'",
				    lines[i]);
			return FALSE;
		}
		key = fu_strstrip(kv[0]);
		if (g_strcmp0(key, kv[0]) != 0)
			g_debug("removing DS-20 whitespace '%s'", kv[0]);
		value = fu_strstrip(kv[1]);
		if (g_strcmp0(value, kv[1]) != 0)
			g_debug("removing DS-20 whitespace '%s'", kv[1]);

		/* it's fine to be strict here, as we checked the fwupd version was new enough in
		 * FuUsbDeviceDs20Item */
		g_debug("setting ds20 device quirk '%s'='%s'", key, value);
		if (!fu_device_set_quirk_kv(FU_DEVICE(device),
					    key,
					    value,
					    FU_CONTEXT_QUIRK_SOURCE_DEVICE,
					    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_usb_device_fw_ds20_class_init(FuUsbDeviceFwDs20Class *klass)
{
	FuUsbDeviceDs20Class *usb_device_ds20_class = FU_USB_DEVICE_DS20_CLASS(klass);
	usb_device_ds20_class->parse = fu_usb_device_fw_ds20_parse;
}

static void
fu_usb_device_fw_ds20_init(FuUsbDeviceFwDs20 *self)
{
	fu_firmware_set_version_raw(FU_FIRMWARE(self), DS20_VERSION_CURRENT);
	fu_usb_device_ds20_set_version_lowest(FU_USB_DEVICE_DS20(self), DS20_VERSION_LOWEST);
	fu_firmware_set_id(FU_FIRMWARE(self), "010aec63-f574-52cd-9dda-2852550d94f0");
}

/**
 * fu_usb_device_fw_ds20_new:
 *
 * Creates a new #FuUsbDeviceFwDs20.
 *
 * Returns: (transfer full): a #FuFirmware
 *
 * Since: 1.8.5
 **/
FuFirmware *
fu_usb_device_fw_ds20_new(void)
{
	return g_object_new(FU_TYPE_USB_DEVICE_FW_DS20, NULL);
}
