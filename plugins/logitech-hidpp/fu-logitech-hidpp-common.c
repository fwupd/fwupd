/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"

#define HIDPP_REPORT_NOTIFICATION 0x01

static gchar *
fu_logitech_hidpp_msg_to_string(FuStructLogitechHidppMsg *st)
{
	g_autofree gchar *base = fu_struct_logitech_hidpp_msg_to_string(st);
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = g_string_new(base);

	if (!fu_logitech_hidpp_msg_is_error(st, &error))
		g_string_append_printf(str, "\nerror:       %s", error->message);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/* filter HID++1.0 messages */
static gboolean
fu_logitech_hidpp_msg_is_hidpp10_compat(FuStructLogitechHidppMsg *st)
{
	guint8 sub_id = fu_struct_logitech_hidpp_msg_get_sub_id(st);
	if (sub_id == 0x40 || sub_id == 0x41 || sub_id == 0x49 || sub_id == 0x4b ||
	    sub_id == 0x8f) {
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_logitech_hidpp_msg_verify_swid(FuStructLogitechHidppMsg *st)
{
	g_return_val_if_fail(st != NULL, FALSE);
	if ((fu_struct_logitech_hidpp_msg_get_function_id(st) & 0x0f) !=
	    FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID)
		return FALSE;
	return TRUE;
}

static gsize
fu_logitech_hidpp_msg_get_payload_length(FuStructLogitechHidppMsg *st)
{
	FuLogitechHidppReportId report_id = fu_struct_logitech_hidpp_msg_get_report_id(st);
	if (report_id == FU_LOGITECH_HIDPP_REPORT_ID_SHORT)
		return 0x07;
	if (report_id == FU_LOGITECH_HIDPP_REPORT_ID_LONG)
		return 0x14;
	if (report_id == FU_LOGITECH_HIDPP_REPORT_ID_VERY_LONG)
		return 0x2f;
	if (report_id == HIDPP_REPORT_NOTIFICATION)
		return 0x08;
	return 0x0;
}

gboolean
fu_logitech_hidpp_send(FuUdevDevice *udev_device,
		       FuStructLogitechHidppMsg *st,
		       guint8 hidpp_version,
		       guint timeout,
		       FuLogitechHidppMsgFlags flags,
		       GError **error)
{
	gsize len = fu_logitech_hidpp_msg_get_payload_length(st);
	FuIoChannelFlags write_flags = FU_IO_CHANNEL_FLAG_FLUSH_INPUT;
	g_autofree gchar *str = NULL;

	/* sanity check */
	if (len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown report_id 0x%02x",
			    fu_struct_logitech_hidpp_msg_get_report_id(st));
		return FALSE;
	}

	/* only for HID++2.0 */
	if (hidpp_version >= 2) {
		fu_struct_logitech_hidpp_msg_set_function_id(
		    st,
		    fu_struct_logitech_hidpp_msg_get_function_id(st) |
			FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID);
	}

	/* force long reports for BLE-direct devices */
	if (hidpp_version == FU_LOGITECH_HIDPP_VERSION_BLE) {
		fu_struct_logitech_hidpp_msg_set_report_id(st, FU_LOGITECH_HIDPP_REPORT_ID_LONG);
		len = 20;
	}
	fu_dump_raw(G_LOG_DOMAIN, "host->device", st->buf->data, MIN(st->buf->len, len));

	/* debugging */
	str = fu_logitech_hidpp_msg_to_string(st);
	g_debug("%s", str);

	/* only use blocking IO when it will be a short timeout for reboot */
	if ((flags & FU_LOGITECH_HIDPP_MSG_FLAG_NON_BLOCKING_IO) == 0)
		write_flags |= FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO;

	/* HID */
	if (!fu_udev_device_write(udev_device,
				  st->buf->data,
				  MIN(st->buf->len, len),
				  timeout,
				  write_flags,
				  error)) {
		g_prefix_error_literal(error, "failed to send: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

FuStructLogitechHidppMsg *
fu_logitech_hidpp_receive(FuUdevDevice *udev_device, guint timeout, GError **error)
{
	gsize read_size = 0;
	gsize bufsz = FU_STRUCT_LOGITECH_HIDPP_MSG_SIZE;
	g_autoptr(FuStructLogitechHidppMsg) st = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* the emulations were captured with the junk data */
	if (fu_device_has_flag(FU_DEVICE(udev_device), FWUPD_DEVICE_FLAG_EMULATED) &&
	    !fu_device_check_fwupd_version(FU_DEVICE(udev_device), "2.1.1"))
		bufsz += 5;

	fu_byte_array_set_size(buf, bufsz, 0x0);
	if (!fu_udev_device_read(udev_device,
				 buf->data,
				 buf->len,
				 &read_size,
				 timeout,
				 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				 error)) {
		g_prefix_error_literal(error, "failed to receive: ");
		return NULL;
	}

	/* check long enough, but allow returning oversize packets */
	fu_dump_raw(G_LOG_DOMAIN, "device->host", buf->data, read_size);
	st = fu_struct_logitech_hidpp_msg_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return NULL;
	if (read_size < fu_logitech_hidpp_msg_get_payload_length(st)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "message length too small, "
			    "got %" G_GSIZE_FORMAT " expected %" G_GSIZE_FORMAT,
			    read_size,
			    fu_logitech_hidpp_msg_get_payload_length(st));
		return NULL;
	}

	/* success */
	return g_steal_pointer(&st);
}

FuStructLogitechHidppMsg *
fu_logitech_hidpp_transfer(FuUdevDevice *udev_device,
			   FuStructLogitechHidppMsg *st,
			   guint8 hidpp_version,
			   FuLogitechHidppMsgFlags flags,
			   GError **error)
{
	guint timeout = FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS;

	/* increase timeout for some operations */
	if (flags & FU_LOGITECH_HIDPP_MSG_FLAG_NON_BLOCKING_IO)
		timeout *= 10;

	/* send request */
	if (!fu_logitech_hidpp_send(udev_device, st, hidpp_version, timeout, flags, error))
		return NULL;

	/* keep trying to receive until we get a valid reply */
	for (guint i = 0; i < 10; i++) {
		g_autoptr(FuStructLogitechHidppMsg) st_tmp = NULL;

		if (flags & FU_LOGITECH_HIDPP_MSG_FLAG_RETRY_STUCK) {
			g_autoptr(GError) error_local = NULL;
			/* retry the send once case the device is "stuck" */
			st_tmp = fu_logitech_hidpp_receive(udev_device, 1000, &error_local);
			if (st_tmp == NULL) {
				g_debug("ignoring: %s", error_local->message);
				if (!fu_logitech_hidpp_send(udev_device,
							    st,
							    hidpp_version,
							    timeout,
							    flags,
							    error))
					return NULL;
			}
		}
		if (st_tmp == NULL) {
			st_tmp = fu_logitech_hidpp_receive(udev_device, timeout, error);
			if (st_tmp == NULL)
				return NULL;
		}

		/* we don't know how to handle this report packet */
		if (fu_logitech_hidpp_msg_get_payload_length(st_tmp) == 0x0) {
			g_debug("HID++1.0 report 0x%02x has unknown length, ignoring",
				fu_struct_logitech_hidpp_msg_get_report_id(st_tmp));
			continue;
		}

		/* maybe something is also writing to the device? --
		 * we can't use the SwID as this is a HID++2.0 feature */
		if (!fu_logitech_hidpp_msg_is_error(st_tmp, error))
			return NULL;

		/* is valid reply */
		if (fu_logitech_hidpp_msg_is_reply(st, st_tmp, flags))
			return g_steal_pointer(&st_tmp);

		/* to ensure compatibility when an HID++ 2.0 device is
		 * connected to an HID++ 1.0 receiver, any feature index
		 * corresponding to an HID++ 1.0 sub-identifier which could be
		 * sent by the receiver, must be assigned to a dummy feature */
		if (hidpp_version >= 2) {
			if (fu_logitech_hidpp_msg_is_hidpp10_compat(st_tmp)) {
				g_debug("ignoring HID++1.0 reply");
				continue;
			}

			/* not us */
			if ((flags & FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_SWID) == 0) {
				if (!fu_logitech_hidpp_msg_verify_swid(st_tmp)) {
					g_debug(
					    "ignoring reply with SwId 0x%02i, expected 0x%02i",
					    fu_struct_logitech_hidpp_msg_get_function_id(st_tmp) &
						0x0f,
					    FU_LOGITECH_HIDPP_HIDPP_MSG_SW_ID);
					continue;
				}
			}
		}

		/* hardware not responding */
		g_debug("ignoring message %u", i);
	};

	/* copy over data */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "too many messages to ignore");
	return NULL;
}

gboolean
fu_logitech_hidpp_msg_is_reply(FuStructLogitechHidppMsg *st1,
			       FuStructLogitechHidppMsg *st2,
			       FuLogitechHidppMsgFlags flags)
{
	g_return_val_if_fail(st1 != NULL, FALSE);
	g_return_val_if_fail(st2 != NULL, FALSE);
	if (fu_struct_logitech_hidpp_msg_get_device_id(st1) !=
		fu_struct_logitech_hidpp_msg_get_device_id(st2) &&
	    fu_struct_logitech_hidpp_msg_get_device_id(st1) != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED &&
	    fu_struct_logitech_hidpp_msg_get_device_id(st2) != FU_LOGITECH_HIDPP_DEVICE_IDX_WIRED)
		return FALSE;
	if ((flags & FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_SUB_ID) == 0 &&
	    fu_struct_logitech_hidpp_msg_get_sub_id(st1) !=
		fu_struct_logitech_hidpp_msg_get_sub_id(st2))
		return FALSE;
	if ((flags & FU_LOGITECH_HIDPP_MSG_FLAG_IGNORE_FNCT_ID) == 0 &&
	    fu_struct_logitech_hidpp_msg_get_function_id(st1) !=
		fu_struct_logitech_hidpp_msg_get_function_id(st2))
		return FALSE;
	return TRUE;
}

/* HID++ error */
gboolean
fu_logitech_hidpp_msg_is_error(FuStructLogitechHidppMsg *st, GError **error)
{
	guint8 sub_id = fu_struct_logitech_hidpp_msg_get_sub_id(st);
	if (sub_id == FU_LOGITECH_HIDPP_SUBID_ERROR_MSG) {
		const guint8 *data = fu_struct_logitech_hidpp_msg_get_data(st, NULL);
		const gchar *str = fu_logitech_hidpp_err_to_string(data[1]);
		const FuErrorMapEntry entries[] = {
		    {FU_LOGITECH_HIDPP_ERR_INVALID_SUBID, FWUPD_ERROR_NOT_SUPPORTED, str},
		    {FU_LOGITECH_HIDPP_ERR_TOO_MANY_DEVICES, FWUPD_ERROR_NOT_SUPPORTED, str},
		    {FU_LOGITECH_HIDPP_ERR_REQUEST_UNAVAILABLE, FWUPD_ERROR_NOT_SUPPORTED, str},
		    {FU_LOGITECH_HIDPP_ERR_INVALID_ADDRESS, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR_INVALID_VALUE, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR_ALREADY_EXISTS, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR_INVALID_PARAM_VALUE, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR_CONNECT_FAIL, FWUPD_ERROR_INTERNAL, str},
		    {FU_LOGITECH_HIDPP_ERR_BUSY, FWUPD_ERROR_BUSY, str},
		    {FU_LOGITECH_HIDPP_ERR_UNKNOWN_DEVICE, FWUPD_ERROR_NOT_FOUND, str},
		    {FU_LOGITECH_HIDPP_ERR_RESOURCE_ERROR, FWUPD_ERROR_NOT_FOUND, str},
		    {FU_LOGITECH_HIDPP_ERR_WRONG_PIN_CODE, FWUPD_ERROR_AUTH_FAILED, str},
		};
		return fu_error_map_entry_to_gerror(data[1], entries, G_N_ELEMENTS(entries), error);
	}
	if (sub_id == FU_LOGITECH_HIDPP_SUBID_ERROR_MSG_20) {
		const guint8 *data = fu_struct_logitech_hidpp_msg_get_data(st, NULL);
		const gchar *str = fu_logitech_hidpp_err2_to_string(data[1]);
		const FuErrorMapEntry entries[] = {
		    {FU_LOGITECH_HIDPP_ERR2_INVALID_ARGUMENT, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR2_OUT_OF_RANGE, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR2_HW_ERROR, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR2_INVALID_FEATURE_INDEX, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR2_INVALID_FUNCTION_ID, FWUPD_ERROR_INVALID_DATA, str},
		    {FU_LOGITECH_HIDPP_ERR2_BUSY, FWUPD_ERROR_BUSY, str},
		    {FU_LOGITECH_HIDPP_ERR2_UNSUPPORTED, FWUPD_ERROR_NOT_SUPPORTED, str},
		};
		return fu_error_map_entry_to_gerror(data[1], entries, G_N_ELEMENTS(entries), error);
	}
	return TRUE;
}

gchar *
fu_logitech_hidpp_format_version(const gchar *name, guint8 major, guint8 minor, guint16 build)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; i < 3; i++) {
		if (g_ascii_isspace(name[i]) || name[i] == '\0')
			continue;
		g_string_append_c(str, name[i]);
	}
	g_string_append_printf(str, "%02x.%02x_B%04x", major, minor, build);
	return g_string_free(str, FALSE);
}
