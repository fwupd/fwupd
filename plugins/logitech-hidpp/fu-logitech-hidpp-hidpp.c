/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-hidpp.h"

static gchar *
fu_logitech_hidpp_msg_to_string (FuLogitechHidPpHidppMsg *msg)
{
	GString *str = g_string_new (NULL);
	const gchar *tmp;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) flags_str = g_string_new (NULL);

	g_return_val_if_fail (msg != NULL, NULL);

	if (msg->flags == FU_UNIFYING_HIDPP_MSG_FLAG_NONE) {
		g_string_append (flags_str, "none");
	} else {
		if (msg->flags & FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT)
			g_string_append (flags_str, "longer-timeout,");
		if (msg->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SUB_ID)
			g_string_append (flags_str, "ignore-sub-id,");
		if (msg->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_FNCT_ID)
			g_string_append (flags_str, "ignore-fnct-id,");
		if (msg->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SWID)
			g_string_append (flags_str, "ignore-swid,");
		if (str->len > 0)
			g_string_truncate (str, str->len - 1);
	}
	g_string_append_printf (str, "flags:       %02x   [%s]\n",
				msg->flags,
				flags_str->str);
	g_string_append_printf (str, "report-id:   %02x   [%s]\n",
				msg->report_id,
				fu_logitech_hidpp_msg_rpt_id_to_string (msg));
	tmp = fu_logitech_hidpp_msg_dev_id_to_string (msg);
	g_string_append_printf (str, "device-id:   %02x   [%s]\n",
				msg->device_id, tmp );
	g_string_append_printf (str, "sub-id:      %02x   [%s]\n",
				msg->sub_id,
				fu_logitech_hidpp_msg_sub_id_to_string (msg));
	g_string_append_printf (str, "function-id: %02x   [%s]\n",
				msg->function_id,
				fu_logitech_hidpp_msg_fcn_id_to_string (msg));
	if (!fu_logitech_hidpp_msg_is_error (msg, &error)) {
		g_string_append_printf (str, "error:       %s\n",
					error->message);
	}
	return g_string_free (str, FALSE);
}

gboolean
fu_logitech_hidpp_send (FuIOChannel *io_channel,
			FuLogitechHidPpHidppMsg *msg,
			guint timeout,
			GError **error)
{
	gsize len = fu_logitech_hidpp_msg_get_payload_length (msg);
	FuIOChannelFlags write_flags = FU_IO_CHANNEL_FLAG_FLUSH_INPUT;

	/* only for HID++2.0 */
	if (msg->hidpp_version >= 2.f)
		msg->function_id |= FU_UNIFYING_HIDPP_MSG_SW_ID;

	/* detailed debugging */
	if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL) {
		g_autofree gchar *str = fu_logitech_hidpp_msg_to_string (msg);
		fu_common_dump_raw (G_LOG_DOMAIN, "host->device", (guint8 *) msg, len);
		g_print ("%s", str);
	}

	/* only use blocking IO when it will be a short timeout for reboot */
	if ((msg->flags & FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT) == 0)
		write_flags |= FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO;

	/* HID */
	if (!fu_io_channel_write_raw (io_channel, (guint8 *) msg, len, timeout,
				      write_flags, error)) {
		g_prefix_error (error, "failed to send: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_logitech_hidpp_receive (FuIOChannel *io_channel,
			   FuLogitechHidPpHidppMsg *msg,
			   guint timeout,
			   GError **error)
{
	gsize read_size = 0;

	if (!fu_io_channel_read_raw (io_channel,
				     (guint8 *) msg,
				     sizeof(FuLogitechHidPpHidppMsg),
				     &read_size,
				     timeout,
				     FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				     error)) {
		g_prefix_error (error, "failed to receive: ");
		return FALSE;
	}

	/* check long enough, but allow returning oversize packets */
	if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "device->host", (guint8 *) msg, read_size);
	if (read_size < fu_logitech_hidpp_msg_get_payload_length (msg)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "message length too small, "
			     "got %" G_GSIZE_FORMAT " expected %" G_GSIZE_FORMAT,
			     read_size, fu_logitech_hidpp_msg_get_payload_length (msg));
		return FALSE;
	}

	/* detailed debugging */
	if (g_getenv ("FWUPD_UNIFYING_VERBOSE") != NULL) {
		g_autofree gchar *str = fu_logitech_hidpp_msg_to_string (msg);
		g_print ("%s", str);
	}

	/* success */
	return TRUE;
}

gboolean
fu_logitech_hidpp_transfer (FuIOChannel *io_channel, FuLogitechHidPpHidppMsg *msg, GError **error)
{
	guint timeout = FU_UNIFYING_DEVICE_TIMEOUT_MS;
	guint ignore_cnt = 0;
	g_autoptr(FuLogitechHidPpHidppMsg) msg_tmp = fu_logitech_hidpp_msg_new ();

	/* increase timeout for some operations */
	if (msg->flags & FU_UNIFYING_HIDPP_MSG_FLAG_LONGER_TIMEOUT)
		timeout *= 10;

	/* send request */
	if (!fu_logitech_hidpp_send (io_channel, msg, timeout, error))
		return FALSE;

	/* keep trying to receive until we get a valid reply */
	while (1) {
		msg_tmp->hidpp_version = msg->hidpp_version;
		if (!fu_logitech_hidpp_receive (io_channel, msg_tmp, timeout, error)) {
			g_prefix_error (error, "failed to receive: ");
			return FALSE;
		}

		/* we don't know how to handle this report packet */
		if (fu_logitech_hidpp_msg_get_payload_length (msg_tmp) == 0x0) {
			g_debug ("HID++1.0 report 0x%02x has unknown length, ignoring",
				 msg_tmp->report_id);
			continue;
		}

		/* maybe something is also writing to the device? --
		 * we can't use the SwID as this is a HID++2.0 feature */
		if (!fu_logitech_hidpp_msg_is_error (msg_tmp, error))
			return FALSE;

		/* is valid reply */
		if (fu_logitech_hidpp_msg_is_reply (msg, msg_tmp))
			break;

		/* to ensure compatibility when an HID++ 2.0 device is
		 * connected to an HID++ 1.0 receiver, any feature index
		 * corresponding to an HID++ 1.0 sub-identifier which could be
		 * sent by the receiver, must be assigned to a dummy feature */
		if (msg->hidpp_version >= 2.f) {
			if (fu_logitech_hidpp_msg_is_hidpp10_compat (msg_tmp)) {
				g_debug ("ignoring HID++1.0 reply");
				continue;
			}

			/* not us */
			if ((msg->flags & FU_UNIFYING_HIDPP_MSG_FLAG_IGNORE_SWID) == 0) {
				if (!fu_logitech_hidpp_msg_verify_swid (msg_tmp)) {
					g_debug ("ignoring reply with SwId 0x%02i, expected 0x%02i",
						 msg_tmp->function_id & 0x0f,
						 FU_UNIFYING_HIDPP_MSG_SW_ID);
					continue;
				}
			}
		}

		/* hardware not responding */
		if (ignore_cnt++ > 10) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "too many messages to ignore");
			return FALSE;
		}

		g_debug ("ignoring message %u", ignore_cnt);
	};

	/* copy over data */
	fu_logitech_hidpp_msg_copy (msg, msg_tmp);
	return TRUE;
}
