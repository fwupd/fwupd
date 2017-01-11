/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
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
#include <glib-object.h>
#include <smbios_c/system_info.h>

#include "synapticsmst-device.h"
#include "synapticsmst-common.h"

#define BLOCK_UNIT	64

typedef struct
{
	SynapticsMSTDeviceKind	 kind;
	const gchar		*devfs_node;
	gchar			*version;
	SynapticsMSTDeviceBoardID boardID;
	gchar			*chipID;
	gchar			*guid;
} SynapticsMSTDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SynapticsMSTDevice, synapticsmst_device, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (synapticsmst_device_get_instance_private (o))

/**
 * synapticsmst_device_kind_from_string:
 * @kind: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: (transfer full): a #SynapticsMSTDeviceKind, or %SYNAPTICSMST_DEVICE_KIND_UNKNOWN for unknown.
 *
 * Since: 0.1.0
 **/
SynapticsMSTDeviceKind
synapticsmst_device_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "DIRECT") == 0)
		return SYNAPTICSMST_DEVICE_KIND_DIRECT;
	if (g_strcmp0 (kind, "REMOTE") == 0)
		return SYNAPTICSMST_DEVICE_KIND_REMOTE;
	return SYNAPTICSMST_DEVICE_KIND_UNKNOWN;
}

/**
 * synapticsmst_device_kind_to_string:
 * @kind: the #SynapticsMSTDeviceKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.1.0
 **/
const gchar *
synapticsmst_device_kind_to_string (SynapticsMSTDeviceKind kind)
{
	if (kind == SYNAPTICSMST_DEVICE_KIND_DIRECT)
		return "DIRECT";
	if (kind == SYNAPTICSMST_DEVICE_KIND_REMOTE)
		return "REMOTE";
	return NULL;
}

const gchar *
synapticsmst_device_boardID_to_string (SynapticsMSTDeviceBoardID boardID)
{
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_SYNA_EVB)
		return "SYNA evb baord";
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_X6)
		return "Dell X6 Dock";
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_X7)
		return "Dell X7 Dock";
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_TRINITY_WIRE)
		return "Dell Trinity Wire Dock";
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_TRINITY_WIRELESS)
		return "Dell Trinity Wireless Dock";
	return NULL;
}

/**
 * synapticsmst_device_get_guid:
 *
 * Returns: string version of GUID for use with fwupd
 *
 **/
const gchar *
synapticsmst_device_get_guid (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->guid;
}

static void
synapticsmst_device_finalize (GObject *object)
{
	SynapticsMSTDevice *device = SYNAPTICSMST_DEVICE (object);
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	g_free (priv->version);
	g_free (priv->chipID);
	g_free (priv->guid);
	G_OBJECT_CLASS (synapticsmst_device_parent_class)->finalize (object);
}

static void
synapticsmst_device_init (SynapticsMSTDevice *device)
{
}

static void
synapticsmst_device_class_init (SynapticsMSTDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = synapticsmst_device_finalize;
}

/**
 * synapticsmst_device_get_kind:
 * @device: a #SynapticsMSTDevice instance.
 *
 * Gets the device kind.
 *
 * Returns: the #SynapticsMSTDeviceKind
 *
 * Since: 0.1.0
 **/
SynapticsMSTDeviceKind
synapticsmst_device_get_kind (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->kind;
}

SynapticsMSTDeviceBoardID
synapticsmst_device_get_boardID (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->boardID;
}

gboolean
synapticsmst_device_enable_remote_control (SynapticsMSTDevice *device, GError **error)
{
	const gchar *sc = "PRIUS";
	if (synapticsmst_common_rc_set_command (UPDC_ENABLE_RC, 5, 0, (guchar*)sc)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to enable MST remote control");
		return FALSE;
	} else {
		return TRUE;
	}
}

gboolean
synapticsmst_device_disable_remote_control (SynapticsMSTDevice *device, GError **error)
{
	if (synapticsmst_common_rc_set_command (UPDC_DISABLE_RC, 0, 0, (guchar*)NULL)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to disable MST remote control");
		return FALSE;
	} else {
		return TRUE;
	}
}

gboolean
synapticsmst_device_enumerate_device (SynapticsMSTDevice *device, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	guint8 byte[16];
	guint16 system_id;

	if (synapticsmst_common_open_aux_node(priv->devfs_node)) {
		guint8 ret;
		// enable remote control
		if (!synapticsmst_device_enable_remote_control(device, error)) {
			return FALSE;
		}

		// read firmware version
		ret = synapticsmst_common_read_dpcd (REG_FIRMWARE_VERSIOIN, (gint *)byte, 3);
		if (ret) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read dpcd from device");
			return FALSE;
		}
		priv->version = g_strdup_printf("%1d.%02d.%03d", byte[0], byte[1], byte[2]);

		// read board ID
		synapticsmst_common_rc_get_command(UPDC_READ_FROM_EEPROM, 2, ADDR_CUSTOMER_ID, byte);
		if (ret) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read from EEPROM of device");
			return FALSE;
		}
		if (byte[0] == 0x01) {
			priv->boardID = (byte[0] << 8) | (byte[1]);
		} else if (byte[0] == 0x00) {
			priv->boardID = (byte[0] << 8) | (byte[1]); // remove this when release
		} else {
			priv->boardID = 0;
		}

		// read board chipID
		synapticsmst_common_read_dpcd (REG_CHIP_ID, (gint *)byte, 2);
		if (ret) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read dpcd from device");
			return FALSE;
		}
		priv->chipID = g_strdup_printf("VMM%02x%02x", byte[0], byte[1]);

		/* set up GUID */
		/* If this is a dock, ignore system ID */
		system_id = (guint16) sysinfo_get_dell_system_id ();

		if ((priv->boardID == SYNAPTICSMST_DEVICE_BOARDID_TRINITY_WIRE) ||
		    (priv->boardID == SYNAPTICSMST_DEVICE_BOARDID_TRINITY_WIRELESS))
			priv->guid = g_strdup_printf ("MST-dell-bmeie-%u",
						      priv->boardID);
		/* This is a host system */
		else
			priv->guid = g_strdup_printf ("MST-dell-%04x-%u",
						      system_id, priv->boardID);

		// disable remote control and close aux node
		synapticsmst_device_disable_remote_control(device,error);
		synapticsmst_common_close_aux_node();
	} else {
		//g_print ("fail to open aux node %s\n", priv->devfs_node);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to open device in DP Aux Node %d",
			     synapticsmst_device_get_aux_node_to_int (device));
		return FALSE;
	}

	return TRUE;
}

const gchar *
synapticsmst_device_get_version (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->version;
}

const gchar *
synapticsmst_device_get_chipID (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->chipID;
}

static gboolean
synapticsmst_device_get_flash_checksum (SynapticsMSTDevice *device, gint length, gint offset, guint32 *checksum, GError **error)
{
	if (synapticsmst_common_rc_special_get_command (UPDC_CAL_EEPROM_CHECKSUM,
							length, offset,
							NULL, 4,
							(guchar *) checksum)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to get flash checksum");
		return FALSE;
	} else {
		return TRUE;
	}
}

gboolean
synapticsmst_device_write_firmware (SynapticsMSTDevice *device,
				    GBytes *fw,
				    GFileProgressCallback progress_cb,
				    gpointer progress_data,
				    GError **error)
{
	const guint8 *payload_data;
	guint32 payload_len;
	guint32 code_size = 0;
	guint32 checksum = 0;
	guint32 offset = 0;
	guint32 write_loops = 0;
	guint32 data_to_write = 0;
	guint8 percentage = 0;
	guint8 ret = 0;
	guint16 tmp;

	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	// get firmware data and check size
	payload_data = g_bytes_get_data(fw, NULL);
	payload_len = g_bytes_get_size(fw);
	if (payload_len > 0x10000 || payload_len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware: invalid file size");
		return FALSE;
	}

	// check firmware content
	for (guint8 i = 0; i < 128; i++) {
		checksum += *(payload_data + i);
	}
	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware: EDID checksum error");
		return FALSE;
	}

	checksum = 0;
	offset = 128;
	for (guint8 i = 0; i < 128; i++) {
		checksum += *(payload_data + offset + i);
	}
	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware: EDID checksum error");
		return FALSE;
	}

	checksum = 0;
	offset = 0x100;
	for (guint16 i = 0; i < 256; i++) {
		checksum += *(payload_data + offset + i);
	}
	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware : configuration checksum error");
		return FALSE;
	}

	checksum = 0;
	offset = 0x200;
	for (guint16 i = 0; i < 256; i++) {
		checksum += *(payload_data + offset + i);
	}
	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware : configuration checksum error");
		return FALSE;
	}

	checksum = 0;
	offset = 0x400;
	code_size = (*(payload_data + offset) << 8) + *(payload_data + offset + 1);
	if (code_size >= 0xFFFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware: invalid firmware size");
		return FALSE;
	}
	for (guint32 i = 0; i < (code_size + 17); i++) {
		checksum += *(payload_data + offset + i);
	}
	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware: firmware checksum error");
		return FALSE;
	}

	// check firmware and board ID again
	tmp = (*(payload_data + 0x10E) << 8) + *(payload_data + 0x10F);
	if (tmp != synapticsmst_device_get_boardID(device)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware: board ID mismatch");
		return FALSE;
	}

	if (synapticsmst_common_open_aux_node(priv->devfs_node)) {
		guint16 erase_ctrl = 0xFFFF;

		// enable remote control
		if (!synapticsmst_device_enable_remote_control(device, error)) {
			return FALSE;
		}

		// erase SPI flash
		if (synapticsmst_common_rc_set_command (UPDC_FLASH_ERASE, 2, 0, (guint8 *)&erase_ctrl)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to flash firmware: can't erase flash");
			return FALSE;
		}

		// update firmware
		//g_print ("Writing FW to %s\n", priv->devfs_node);
		write_loops = (payload_len / BLOCK_UNIT);
		data_to_write = payload_len;
		ret = 0;
		offset = 0;

		if (payload_len % BLOCK_UNIT) {
			write_loops++;
		}

		if (progress_cb == NULL)
			g_print ("updating... 0%%");

		for (guint32 i = 0; i < write_loops; i++) {
			guint8 length = BLOCK_UNIT;
			if (data_to_write < BLOCK_UNIT) {
				length = data_to_write;
			}

			ret = synapticsmst_common_rc_set_command (UPDC_WRITE_TO_EEPROM,
								  length, offset,
								  payload_data + offset);
			if (ret) {
				ret = synapticsmst_common_rc_set_command (UPDC_WRITE_TO_EEPROM,
									  length, offset,
									  payload_data + offset); // repeat once
			}

			if (ret) {
				break;
			}

			offset += length;
			data_to_write -= length;
			percentage = i * 100 / (write_loops - 1);
			if (progress_cb != NULL)
				progress_cb ((goffset) i * 100,
					     (goffset) (write_loops -1) * 100,
					     progress_data);
			else
				g_print ("\rupdating... %d%%", percentage);
		}
		if (progress_cb == NULL)
			g_print ("\n");

		if (ret) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware: "
				     "can't write flash at offset 0x%04x",
				     offset);
		} else {
			guint32 flash_checksum = 0;

			// check data just written
			checksum = 0;
			for (guint32 i = 0; i < payload_len; i++) {
				checksum += *(payload_data + i);
			}

			if (synapticsmst_device_get_flash_checksum (device,
								    payload_len,
								    0,
								    &flash_checksum,
								    error)) {
				if (checksum != flash_checksum) {
					ret = -1;
					g_set_error_literal (error,
							     G_IO_ERROR,
							     G_IO_ERROR_INVALID_DATA,
							     "Failed to flash firmware: "
							     "checksum mismatch");
				}
			} else {
				ret = -1;
			}

		}

		// disable remote control and close aux node
		synapticsmst_device_disable_remote_control(device, error);
		synapticsmst_common_close_aux_node();
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to flash firmware : can't open DP Aux node %d",
			     synapticsmst_device_get_aux_node_to_int (device));
		return FALSE;
	}

	if (ret) {
		return FALSE;
	} else {
		return TRUE;
	}
}

/**
 * synapticsmst_device_new:
 *
 * Creates a new #SynapticsMSTDevice.
 *
 * Returns: (transfer full): a #SynapticsMSTDevice
 *
 * Since: 0.1.0
 **/
SynapticsMSTDevice *
synapticsmst_device_new (SynapticsMSTDeviceKind kind, const gchar *aux_node)
{
	SynapticsMSTDevice *device;
	SynapticsMSTDevicePrivate *priv;

	device = g_object_new (SYNAPTICSMST_TYPE_DEVICE, NULL);

	priv = GET_PRIVATE (device);
	priv->devfs_node = aux_node;
	priv->kind = kind;
	priv->version = NULL;

	return SYNAPTICSMST_DEVICE (device);
}

const gchar *
synapticsmst_device_get_aux_node (guint8 index)
{
	if (index == 0) {
		return "/dev/drm_dp_aux0";
	} else if (index == 1) {
		return "/dev/drm_dp_aux1";
	} else if (index == 2) {
		return "/dev/drm_dp_aux2";
	} else {
		return "";
	}
}

guint8
synapticsmst_device_get_aux_node_to_int (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	if (g_strcmp0(priv->devfs_node, "/dev/drm_dp_aux0") == 0) {
		return 0;
	}
	if (g_strcmp0(priv->devfs_node, "/dev/drm_dp_aux1") == 0) {
		return 1;
	}
	if (g_strcmp0(priv->devfs_node, "/dev/drm_dp_aux2") == 0) {
		return 2;
	}
	return 0xFF;
}
