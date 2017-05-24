/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>

#include "lu-common.h"
#include "lu-device-peripheral.h"
#include "lu-hidpp.h"

struct _LuDevicePeripheral
{
	LuDevice	 parent_instance;
	guint8		 cached_fw_entity;
};

G_DEFINE_TYPE (LuDevicePeripheral, lu_device_peripheral, LU_TYPE_DEVICE)

static gboolean
lu_device_peripheral_fetch_firmware_info (LuDevice *device, GError **error)
{
	LuDevicePeripheral *self = LU_DEVICE_PERIPHERAL (device);
	guint8 idx;
	guint8 entity_count;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();

	/* get the feature index */
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_I_FIRMWARE_INFO);
	if (idx == 0x00)
		return TRUE;

	/* get the entity count */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = lu_device_get_hidpp_id (device);
	msg->sub_id = idx;
	msg->function_id = 0x00 << 4; /* getCount */
	if (!lu_device_hidpp_transfer (device, msg, error)) {
		g_prefix_error (error, "failed to get firmware count: ");
		return FALSE;
	}
	entity_count = msg->data[0];

	/* get firmware, bootloader, hardware versions */
	for (guint8 i = 0; i < entity_count; i++) {
		guint16 build;
		g_autofree gchar *version = NULL;
		g_autofree gchar *name = NULL;

		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = lu_device_get_hidpp_id (device);
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* getInfo */
		msg->data[0] = i;
		if (!lu_device_hidpp_transfer (device, msg, error)) {
			g_prefix_error (error, "failed to get firmware info: ");
			return FALSE;
		}
		if (msg->data[1] == 0x00 &&
		    msg->data[2] == 0x00 &&
		    msg->data[3] == 0x00 &&
		    msg->data[4] == 0x00 &&
		    msg->data[4] == 0x00 &&
		    msg->data[5] == 0x00 &&
		    msg->data[6] == 0x00 &&
		    msg->data[7] == 0x00) {
			g_debug ("no version set for entity %u", i);
			continue;
		}
		name = g_strdup_printf ("%c%c%c",
					msg->data[1],
					msg->data[2],
					msg->data[3]);
		build = ((guint16) msg->data[6]) << 8 | msg->data[7];
		version = lu_format_version (name,
					     msg->data[4],
					     msg->data[5],
					     build);
		g_debug ("firmware entity 0x%02x version is %s", i, version);
		if (msg->data[0] == 0) {
			lu_device_set_version_fw (device, version);
			self->cached_fw_entity = i;
		} else if (msg->data[0] == 1) {
			lu_device_set_version_bl (device, version);
		}
	}

	/* not an error, the device just doesn't support this */
	return TRUE;
}

static gboolean
lu_device_peripheral_fetch_battery_level (LuDevice *device, GError **error)
{
	guint8 idx;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();

	/* try using HID++2.0 */
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_BATTERY_LEVEL_STATUS);
	if (idx != 0x00) {
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = lu_device_get_hidpp_id (device);
		msg->sub_id = idx;
		msg->function_id = 0x00; /* GetBatteryLevelStatus */
		if (!lu_device_hidpp_transfer (device, msg, error)) {
			g_prefix_error (error, "failed to get battery info: ");
			return FALSE;
		}
		if (msg->data[0] != 0x00)
			lu_device_set_battery_level (device, msg->data[0]);
		return TRUE;
	}

	/* try HID++1.0 battery mileage */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = lu_device_get_hidpp_id (device);
	msg->sub_id = HIDPP_SUBID_GET_REGISTER;
	msg->function_id = HIDPP_REGISTER_BATTERY_MILEAGE;
	if (lu_device_hidpp_transfer (device, msg, NULL)) {
		if (msg->data[0] != 0x00)
			lu_device_set_battery_level (device, msg->data[0]);
		return TRUE;
	}

	/* try HID++1.0 battery status instead */
	msg->function_id = HIDPP_REGISTER_BATTERY_STATUS;
	if (lu_device_hidpp_transfer (device, msg, NULL)) {
		switch (msg->data[0]) {
		case 1: /* 0 - 10 */
			lu_device_set_battery_level (device, 5);
			break;
		case 3: /* 11 - 30 */
			lu_device_set_battery_level (device, 20);
			break;
		case 5: /* 31 - 80 */
			lu_device_set_battery_level (device, 55);
			break;
		case 7: /* 81 - 100 */
			lu_device_set_battery_level (device, 90);
			break;
		default:
			g_warning ("unknown battery percentage: 0x%02x",
				   msg->data[0]);
			break;
		}
		return TRUE;
	}

	/* not an error, the device just doesn't support any of the methods */
	return TRUE;
}

static gboolean
lu_device_peripheral_ping (LuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();

	/* handle failure */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = lu_device_get_hidpp_id (device);
	msg->sub_id = 0x00; /* rootIndex */
	msg->function_id = 0x01 << 4; /* ping */
	msg->data[0] = 0x00;
	msg->data[1] = 0x00;
	msg->data[2] = 0xaa; /* user-selected value */
	if (!lu_device_hidpp_transfer (device, msg, &error_local)) {
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED)) {
			lu_device_set_hidpp_version (device, 0x01);
			return TRUE;
		}
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_HOST_UNREACHABLE)) {
			lu_device_remove_flag (device, LU_DEVICE_FLAG_ACTIVE);
		}
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to ping device: %s",
			     error_local->message);
		return FALSE;
	}

	/* not sure why this isn't set */
	if (msg->data[0] != 0x02) {
		g_debug ("HID++ version %u implausible, using 2.0",
			 msg->data[0]);
		lu_device_set_hidpp_version (device, 0x02);
	} else {
		lu_device_set_hidpp_version (device, msg->data[0]);
	}

	/* this device is active right now */
	lu_device_add_flag (device, LU_DEVICE_FLAG_ACTIVE);
	return TRUE;
}

static gboolean
lu_device_peripheral_probe (LuDevice *device, GError **error)
{
	guint8 idx;
	const guint16 map_features[] = {
		HIDPP_FEATURE_I_FIRMWARE_INFO,
		HIDPP_FEATURE_BATTERY_LEVEL_STATUS,
		HIDPP_FEATURE_DFU_CONTROL,
		HIDPP_FEATURE_DFU_CONTROL_SIGNED,
		HIDPP_FEATURE_DFU,
		HIDPP_FEATURE_ROOT };

	/* map some *optional* HID++2.0 features we might use */
	for (guint i = 0; map_features[i] != HIDPP_FEATURE_ROOT; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!lu_device_hidpp_feature_search (device,
						     map_features[i],
						     &error_local)) {
			g_debug ("%s", error_local->message);
		}
	}

	/* ping device to get HID++ version */
	if (!lu_device_peripheral_ping (device, error))
		return FALSE;

	/* get the firmware information */
	if (!lu_device_peripheral_fetch_firmware_info (device, error))
		return FALSE;

	/* get the battery level */
	if (!lu_device_peripheral_fetch_battery_level (device, error))
		return FALSE;

	/* try using HID++2.0 */
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		lu_device_add_flag (device, LU_DEVICE_FLAG_CAN_FLASH);
		lu_device_add_flag (device, LU_DEVICE_FLAG_REQUIRES_DETACH);
	}
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		/* check the feature is available */
		g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();
		msg->report_id = HIDPP_REPORT_ID_SHORT;
		msg->device_id = lu_device_get_hidpp_id (device);
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* getDfuStatus */
		if (!lu_device_hidpp_transfer (device, msg, error)) {
			g_prefix_error (error, "failed to get DFU status: ");
			return FALSE;
		}
		if ((msg->data[2] & 0x01) > 0) {
			g_warning ("DFU mode not available");
		} else {
			lu_device_add_flag (device, LU_DEVICE_FLAG_CAN_FLASH);
			lu_device_add_flag (device, LU_DEVICE_FLAG_REQUIRES_DETACH);
			lu_device_add_flag (device, LU_DEVICE_FLAG_REQUIRES_SIGNED_FIRMWARE);
		}
	}
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_DFU);
	if (idx != 0x00)
		lu_device_add_flag (device, LU_DEVICE_FLAG_REQUIRES_ATTACH);

	/* always success */
	return TRUE;
}

static gboolean
lu_device_peripheral_detach (LuDevice *device, GError **error)
{
	guint8 idx;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();

	/* this requires user action */
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_DFU_CONTROL);
	if (idx != 0x00) {
		msg->report_id = HIDPP_REPORT_ID_LONG;
		msg->device_id = lu_device_get_hidpp_id (device);
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* setDfuControl */
		msg->data[0] = 0x01; /* enterDfu */
		msg->data[1] = 0x00; /* dfuControlParam */
		msg->data[2] = 0x00; /* unused */
		msg->data[3] = 0x00; /* unused */
		msg->data[4] = 'D';
		msg->data[5] = 'F';
		msg->data[6] = 'U';
		if (!lu_device_hidpp_transfer (device, msg, error)) {
			g_prefix_error (error, "failed to put device into DFU mode: ");
			return FALSE;
		}
		lu_device_add_flag (device, LU_DEVICE_FLAG_REQUIRES_RESET);
		return TRUE;
	}

	/* this can reboot all by itself */
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_DFU_CONTROL_SIGNED);
	if (idx != 0x00) {
		msg->report_id = HIDPP_REPORT_ID_LONG;
		msg->device_id = lu_device_get_hidpp_id (device);
		msg->sub_id = idx;
		msg->function_id = 0x01 << 4; /* setDfuControl */
		msg->data[0] = 0x01; /* startDfu */
		msg->data[1] = 0x00; /* dfuControlParam */
		msg->data[2] = 0x00; /* unused */
		msg->data[3] = 0x00; /* unused */
		msg->data[4] = 'D';
		msg->data[5] = 'F';
		msg->data[6] = 'U';
		if (!lu_device_hidpp_transfer (device, msg, error)) {
			g_prefix_error (error, "failed to put device into DFU mode: ");
			return FALSE;
		}

		/* reprobe */
		return lu_device_probe (device, error);
	}

	/* we don't know how */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_FAILED,
		     "no method to detach");
	return FALSE;
}

static gboolean
lu_device_peripheral_check_status (guint8 status, GError **error)
{
	switch (status & 0x7f) {
	case 0x00:
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "invalid status value 0x%02x",
			     status);
		break;
	case 0x01: /* packet success */
	case 0x02: /* DFU success */
	case 0x05: /* DFU success: entity restart required */
	case 0x06: /* DFU success: system restart required */
		/* success */
		return TRUE;
		break;
	case 0x03:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PENDING,
				     "wait for event (command in progress)");
		break;
	case 0x04:
	case 0x10: /* unknown */
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "generic error");
		break;
	case 0x11:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "bad voltage (power too low?)");
		break;
	case 0x12:
	case 0x14: /* bad magic string */
	case 0x21: /* bad firmware */
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unsupported firmware");
		break;
	case 0x13:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unsupported encryption mode");
		break;
	case 0x15:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "erase failure");
		break;
	case 0x16:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "DFU not started");
		break;
	case 0x17:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "bad sequence number");
		break;
	case 0x18:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unsupported command");
		break;
	case 0x19:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "command in progress");
		break;
	case 0x1a:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "address out of range");
		break;
	case 0x1b:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "unaligned address");
		break;
	case 0x1c:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "bad size");
		break;
	case 0x1d:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "missing program data");
		break;
	case 0x1e:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "missing check data");
		break;
	case 0x1f:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "program failed to write");
		break;
	case 0x20:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "program failed to verify");
		break;
	case 0x22:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "firmware check failure");
		break;
	case 0x23:
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "blocked command (restart required)");
		break;
	default:
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "unhandled status value 0x%02x",
			     status);
		break;
	}
	return FALSE;
}

static gboolean
lu_device_peripheral_write_firmware_pkt (LuDevice *device,
					 guint8 idx,
					 guint8 cmd,
					 const guint8 *data,
					 GError **error)
{
	guint32 packet_cnt_be;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();
	g_autoptr(GError) error_local = NULL;

	/* send firmware data */
	msg->report_id = HIDPP_REPORT_ID_LONG;
	msg->device_id = lu_device_get_hidpp_id (device);
	msg->sub_id = idx;
	msg->function_id = cmd << 4; /* dfuStart or dfuCmdDataX */
	memcpy (msg->data, data, 16);
	if (!lu_device_hidpp_transfer (device, msg, &error_local)) {
		g_prefix_error (error, "failed to supply program data: ");
		return FALSE;
	}

	/* check error */
	memcpy (&packet_cnt_be, msg->data, 4);
	g_debug ("packet_cnt=0x%04x", GUINT32_FROM_BE (packet_cnt_be));
	if (lu_device_peripheral_check_status (msg->data[4], &error_local))
		return TRUE;

	/* fatal error */
	if (!g_error_matches (error_local,
			      G_IO_ERROR,
			      G_IO_ERROR_PENDING)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     error_local->message);
		return FALSE;
	}

	/* wait for the HID++ notification */
	g_debug ("ignoring: %s", error_local->message);
	for (guint retry = 0; retry < 10; retry++) {
		g_autoptr(LuDeviceHidppMsg) msg2 = lu_device_hidpp_new ();
		if (!lu_device_hidpp_receive (device, msg2, 15000, error))
			return FALSE;
		if (msg2->report_id == msg->report_id &&
		    msg2->device_id == msg->device_id &&
		    msg2->sub_id == msg->sub_id) {
			g_autoptr(GError) error2 = NULL;
			if (!lu_device_peripheral_check_status (msg2->data[4], &error2)) {
				g_debug ("got %s, waiting a bit longer", error2->message);
				continue;
			}
			return TRUE;
		} else {
			g_debug ("got wrong packet, continue to wait...");
		}
	}

	/* nothing in the queue */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to get event after timeout");
	return FALSE;
}

static gboolean
lu_device_peripheral_write_firmware (LuDevice *device,
				     GBytes *fw,
				     GFileProgressCallback progress_cb,
				     gpointer progress_data,
				     GError **error)
{
	gsize sz = 0;
	guint8 *data;
	guint8 cmd = 0x04;
	guint8 idx;

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "no DFU feature available");
		return FALSE;
	}

	/* flash hardware */
	data = g_bytes_get_data (fw, &sz);
	for (gsize i = 0; i < sz / 16; i++) {

		/* send packet and wait for reply */
		g_debug ("send data at addr=0x%04x", (guint) i * 16);
		if (!lu_device_peripheral_write_firmware_pkt (device,
							      idx,
							      cmd,
							      data + (i * 16),
							      error)) {
			g_prefix_error (error,
					"failed to write @0x%04x: ",
					(guint) i * 16);
			return FALSE;
		}

		/* use sliding window */
		cmd = (cmd + 1) % 4;

		/* update progress-bar */
		if (progress_cb != NULL)
			progress_cb ((goffset) i * 16, (goffset) sz, progress_data);
	}

	return TRUE;
}

static gboolean
lu_device_peripheral_attach (LuDevice *device, GError **error)
{
	LuDevicePeripheral *self = LU_DEVICE_PERIPHERAL (device);
	guint8 idx;
	g_autoptr(LuDeviceHidppMsg) msg = lu_device_hidpp_new ();

	/* if we're in bootloader mode, we should be able to get this feature */
	idx = lu_device_hidpp_feature_get_idx (device, HIDPP_FEATURE_DFU);
	if (idx == 0x00) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "no DFU feature available");
		return FALSE;
	}

	/* reboot back into firmware mode */
	msg->report_id = HIDPP_REPORT_ID_SHORT;
	msg->device_id = lu_device_get_hidpp_id (device);
	msg->sub_id = idx;
	msg->function_id = 0x05 << 4; /* restart */
	msg->data[0] = self->cached_fw_entity; /* fwEntity */
	if (!lu_device_hidpp_transfer (device, msg, error)) {
		g_prefix_error (error, "failed to restart device: ");
		return FALSE;
	}

	/* reprobe */
	if (!lu_device_probe (device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
lu_device_peripheral_finalize (GObject *object)
{
	G_OBJECT_CLASS (lu_device_peripheral_parent_class)->finalize (object);
}

static void
lu_device_peripheral_class_init (LuDevicePeripheralClass *klass)
{
	LuDeviceClass *klass_device = LU_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lu_device_peripheral_finalize;
	klass_device->probe = lu_device_peripheral_probe;
	klass_device->poll = lu_device_peripheral_ping;
	klass_device->write_firmware = lu_device_peripheral_write_firmware;
	klass_device->attach = lu_device_peripheral_attach;
	klass_device->detach = lu_device_peripheral_detach;
}

static void
lu_device_peripheral_init (LuDevicePeripheral *self)
{
}
