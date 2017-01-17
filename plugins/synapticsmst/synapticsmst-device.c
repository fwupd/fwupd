/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
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

#define BLOCK_UNIT		64

typedef struct
{
	SynapticsMSTDeviceKind	 kind;
	const gchar		*devfs_node;
	gchar			*version;
	SynapticsMSTDeviceBoardID boardID;
	gchar			*chipID;
	gchar			*guid;
	guint8			 aux_node;
	guint8			 layer;
	guint16			 rad;
} SynapticsMSTDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SynapticsMSTDevice, synapticsmst_device, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (synapticsmst_device_get_instance_private (o))

SynapticsMSTDeviceKind
synapticsmst_device_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "DIRECT") == 0)
		return SYNAPTICSMST_DEVICE_KIND_DIRECT;
	if (g_strcmp0 (kind, "REMOTE") == 0)
		return SYNAPTICSMST_DEVICE_KIND_REMOTE;
	return SYNAPTICSMST_DEVICE_KIND_UNKNOWN;
}

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
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_DELL_X6)
		return "Dell X6 Platform";
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_DELL_X7)
		return "Dell X7 Platform";
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_DELL_WD15_TB16_WIRE)
		return "Dell WD15/TB16 wired Dock";
	if (boardID == SYNAPTICSMST_DEVICE_BOARDID_DELL_WLD15_WIRELESS)
		return "Dell WLD15 Wireless Dock";
	if ((boardID & 0xFF00) == SYNAPTICSMST_DEVICE_BOARDID_EVB)
		return "SYNA evb board";
	return NULL;
}

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

const gchar *
synapticsmst_device_get_devfs_node (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->devfs_node;
}

gboolean
synapticsmst_device_enable_remote_control (SynapticsMSTDevice *device, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	synapticsmst_common_config_connection(priv->layer, priv->rad);
	if (synapticsmst_common_enable_remote_control ()) {
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
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	synapticsmst_common_config_connection (priv->layer, priv->rad);
	if (synapticsmst_common_disable_remote_control ()) {
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
synapticsmst_device_scan_cascade_device (SynapticsMSTDevice *device, guint8 tx_port)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint8 layer = priv->layer + 1;
	guint16 rad = priv->rad | (tx_port << (2 * (priv->layer)));
	guint byte[4];
	guint8 rc;

	synapticsmst_common_config_connection (layer, rad);
	rc = synapticsmst_common_read_dpcd (REG_RC_CAP, (gint *)byte, 1);
	if (rc == DPCD_SUCCESS ) {
		if (byte[0] & 0x04) {
			synapticsmst_common_read_dpcd (REG_VENDOR_ID, (gint *)byte, 3);
			if (byte[0] == 0x90 && byte[1] == 0xCC && byte[2] == 0x24)
				return TRUE;
		}
	}

	return FALSE;
}

gboolean
synapticsmst_device_enumerate_device (SynapticsMSTDevice *device, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	guint8 byte[16];
	guint16 system_id;

	if (synapticsmst_common_open_aux_node (synapticsmst_device_aux_node_to_string (priv->aux_node))) {
		guint8 rc;

		/* enable remote control */
		if (!synapticsmst_device_enable_remote_control (device, error))
			return FALSE;


		/* read firmware version */
		rc = synapticsmst_common_read_dpcd (REG_FIRMWARE_VERSION, (gint *)byte, 3);
		if (rc) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read dpcd from device");
			return FALSE;
		}
		priv->version = g_strdup_printf ("%1d.%02d.%03d", byte[0], byte[1], byte[2]);

		/* read board ID */
		rc = synapticsmst_common_rc_get_command (UPDC_READ_FROM_EEPROM, 2, ADDR_CUSTOMER_ID, byte);
		if (rc) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read from EEPROM of device");
			return FALSE;
		}
		priv->boardID = (byte[0] << 8) | (byte[1]);
		/* only dell is supported for today */
		if (byte[0] == CUSTOMERID_DELL) {
			/* set up GUID
			 * GUID is MST-$SYSTEMID-$BOARDID
			 * $BOARDID includes CUSTOMERID in first byte, BOARD in second byte */
			system_id = (guint16) sysinfo_get_dell_system_id ();

			/* If this is a dock, ignore system ID */
			if (priv->boardID == SYNAPTICSMST_DEVICE_BOARDID_DELL_WD15_TB16_WIRE) {
				priv->guid = g_strdup_printf ("MST-bmeie-%u",
							      priv->boardID);

			} else if (priv->boardID == SYNAPTICSMST_DEVICE_BOARDID_DELL_WLD15_WIRELESS) {
				priv->guid = g_strdup_printf ("MST-wld15-%u",
							      priv->boardID);
			}
			/* This is a host system */
			else
				priv->guid = g_strdup_printf ("MST-%04x-%u",
							      system_id, priv->boardID);
		/* EVB development board */
		} else if (byte[0] == 0) {
			priv->boardID = (byte[0] << 8 | byte[1]);
		/* unknown */
		} else {
			priv->boardID = 0xFF;
		}

		/* read board chipID */
		rc = synapticsmst_common_read_dpcd (REG_CHIP_ID, (gint *)byte, 2);
		if (rc) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read dpcd from device");
			return FALSE;
		}
		priv->chipID = g_strdup_printf ("VMM%02x%02x", byte[0], byte[1]);

		/* disable remote control and close aux node */
		synapticsmst_device_disable_remote_control (device, error);
		synapticsmst_common_close_aux_node ();
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to open device in DP Aux Node %d",
			     synapticsmst_device_get_aux_node (device));
		return FALSE;
	}

	return TRUE;
}

guint8
synapticsmst_device_get_aux_node (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->aux_node;
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

guint16
synapticsmst_device_get_rad (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->rad;
}

guint8
synapticsmst_device_get_layer (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->layer;
}

static gboolean
synapticsmst_device_get_flash_checksum (SynapticsMSTDevice *device, gint length, gint offset, guint32 *checksum, GError **error)
{
	if (synapticsmst_common_rc_special_get_command (UPDC_CAL_EEPROM_CHECKSUM,
							length, offset,
							NULL, 4,
							(guchar *)checksum)) {
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
	guint32 flash_checksum = 0;
	guint32 offset = 0;
	guint32 write_loops = 0;
	guint32 data_to_write = 0;
	guint8 percentage = 0;
	guint8 rc = 0;
	guint16 tmp;
	guint16 erase_code = 0xFFFF;

	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	/* get firmware data and check size */
	payload_data = g_bytes_get_data (fw, NULL);
	payload_len = g_bytes_get_size (fw);
	if (payload_len > 0x10000 || payload_len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware : invalid file size");
		return FALSE;
	}

	/* check firmware content */
	for (guint8 i = 0; i < 128; i++)
		checksum += *(payload_data + i);

	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware : EDID checksum error");
		return FALSE;
	}

	checksum = 0;
	offset = 128;
	for (guint8 i = 0; i < 128; i++)
		checksum += *(payload_data + offset + i);

	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware : EDID checksum error");
		return FALSE;
	}

	checksum = 0;
	offset = 0x100;
	for (guint16 i = 0; i < 256; i++)
		checksum += *(payload_data + offset + i);

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
				     "Failed to flash firmware : invalid firmware size");
		return FALSE;
	}
	for (guint32 i = 0; i < (code_size + 17); i++)
		checksum += *(payload_data + offset + i);

	if (checksum & 0xFF) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware : firmware checksum error");
		return FALSE;
	}

	/* TODO: May need a way to override this to cover field
	 * issues of invalid firmware flashed*/
	/* check firmware and board ID again */
	tmp = (*(payload_data + ADDR_CUSTOMER_ID) << 8) + *(payload_data + ADDR_BOARD_ID);
	if (tmp != synapticsmst_device_get_boardID (device)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to flash firmware : board ID mismatch");
		return FALSE;
	}

	if (synapticsmst_common_open_aux_node (synapticsmst_device_aux_node_to_string (priv->aux_node))) {
		/* enable remote control */
		if (!synapticsmst_device_enable_remote_control (device, error)) {
			return FALSE;
		}

		/* erase SPI flash */
		if (synapticsmst_common_rc_set_command (UPDC_FLASH_ERASE, 2, 0, (guint8 *)&erase_code)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to flash firmware : can't erase flash");
			return FALSE;
		}

		/* update firmware */
		write_loops = (payload_len / BLOCK_UNIT);
		data_to_write = payload_len;
		rc = 0;
		offset = 0;

		if (payload_len % BLOCK_UNIT) {
			write_loops++;
		}

		if (progress_cb == NULL)
			g_print("updating... 0%%");

		for (guint32 i = 0; i < write_loops; i++) {
			guint8 length = BLOCK_UNIT;
			if (data_to_write < BLOCK_UNIT)
				length = data_to_write;

			rc = synapticsmst_common_rc_set_command (UPDC_WRITE_TO_EEPROM,
						length, offset,
						payload_data + offset);
			if (rc) {
				/* repeat once */
				rc = synapticsmst_common_rc_set_command (UPDC_WRITE_TO_EEPROM,
							length, offset,
							payload_data + offset);
			}

			if (rc)
				break;

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

		if (rc) {
			g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					"Failed to flash firmware : "
					"can't write flash at offset 0x%04x",
					offset);
		} else {
			/* check data just written */
			checksum = 0;
			for (guint32 i = 0; i < payload_len; i++) {
				checksum += *(payload_data + i);
			}

			flash_checksum = 0;
			if (synapticsmst_device_get_flash_checksum (device,
						  payload_len,
						  0,
						  &flash_checksum,
						  error)) {
				if (checksum != flash_checksum) {
					rc = -1;
					g_set_error_literal (error,
							 G_IO_ERROR,
							 G_IO_ERROR_INVALID_DATA,
							 "Failed to flash firmware : checksum mismatch");
				}
			} else {
				rc = -1;
			}

		}

		/* disable remote control and close aux node */
		synapticsmst_device_disable_remote_control (device, error);
		synapticsmst_common_close_aux_node ();
	} else {
		g_set_error (error,
				G_IO_ERROR,
				G_IO_ERROR_INVALID_DATA,
				"Failed to flash firmware : can't open DP Aux node %d",
				synapticsmst_device_get_aux_node (device));
		return FALSE;
	}

	if (rc) {
		return FALSE;
	} else {
		return TRUE;
	}
}

SynapticsMSTDevice *
synapticsmst_device_new (SynapticsMSTDeviceKind kind, guint8 aux_node, guint8 layer, guint16 rad)
{
	SynapticsMSTDevice *device;
	SynapticsMSTDevicePrivate *priv;

	device = g_object_new (SYNAPTICSMST_TYPE_DEVICE, NULL);
	priv = GET_PRIVATE (device);

	priv->aux_node = aux_node;
	priv->kind = kind;
	priv->version = NULL;
	priv->layer = layer;
	priv->rad = rad;

	return SYNAPTICSMST_DEVICE (device);
}

const gchar *
synapticsmst_device_aux_node_to_string (guint8 index)
{
	if (index == 0)
		return "/dev/drm_dp_aux0";
	if (index == 1)
		return "/dev/drm_dp_aux1";
	if (index == 2)
		return "/dev/drm_dp_aux2";
	return "";
}

guint8
synapticsmst_device_string_to_aux_node (const gchar* str)
{
	if (g_strcmp0 (str, "/dev/drm_dp_aux0") == 0)
		return 0;
	if (g_strcmp0 (str, "/dev/drm_dp_aux1") == 0)
		return 1;
	if (g_strcmp0 (str, "/dev/drm_dp_aux2") == 0)
		return 2;
	return 0xFF;
}
