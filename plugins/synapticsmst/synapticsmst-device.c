/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <string.h>
#include <errno.h>
#include <glib-object.h>
#include <fcntl.h>
#include <stdlib.h>
#include "synapticsmst-device.h"
#include "synapticsmst-common.h"
#include "fu-device-locker.h"

#define BLOCK_UNIT		64
#define PAYLOAD_SIZE_64K		0x10000
#define MAX_RETRY_COUNTS		10

typedef struct
{
	SynapticsMSTDeviceKind	 kind;
	gchar			*version;
	SynapticsMSTDeviceBoardID board_id;
	guint16			 chip_id;
	gchar			*chip_id_str;
	GPtrArray		*guids;
	gchar			*aux_node;
	guint8			 layer;
	guint16			 rad;
	gint			 fd;
	gboolean		has_cascade;
	gchar			*fw_dir;
	gboolean		test_mode;
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
synapticsmst_device_board_id_to_string (SynapticsMSTDeviceBoardID board_id)
{
	if (board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_X6)
		return "Dell X6 Platform";
	if (board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_X7)
		return "Dell X7 Platform";
	if (board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_WD15_TB16_WIRE)
		return "Dell WD15/TB16 wired Dock";
	if (board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_WLD15_WIRELESS)
		return "Dell WLD15 Wireless Dock";
	if (board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_X7_RUGGED)
		return "Dell Rugged Platform";
	if ((board_id >> 8) == CUSTOMERID_DELL)
		return "Dell Generic SynapticsMST Device";
	if ((board_id & 0xFF00) == SYNAPTICSMST_DEVICE_BOARDID_EVB)
		return "SYNA evb board";
	return "Unknown Platform";
}

GPtrArray *
synapticsmst_device_get_guids (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->guids;
}

static void
synapticsmst_device_finalize (GObject *object)
{
	SynapticsMSTDevice *device = SYNAPTICSMST_DEVICE (object);
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->fd > 0)
		close (priv->fd);

	g_free (priv->fw_dir);
	g_free (priv->aux_node);
	g_free (priv->version);
	g_free (priv->chip_id_str);
	g_ptr_array_unref (priv->guids);
	G_OBJECT_CLASS (synapticsmst_device_parent_class)->finalize (object);
}

static void
synapticsmst_device_init (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	const gchar *tmp;
	tmp = g_getenv ("FWUPD_SYNAPTICSMST_FW_DIR");
	if (tmp == NULL) {
		priv->test_mode = FALSE;
		priv->fw_dir = g_strdup ("/dev");
	} else {
		priv->test_mode = TRUE;
		priv->fw_dir = g_strdup (tmp);
	}
	priv->guids = g_ptr_array_new_with_free_func (g_free);
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
synapticsmst_device_get_board_id (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->board_id;
}

static gboolean
synapticsmst_device_enable_remote_control (SynapticsMSTDevice *device, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	/* in test mode we need to open a different file node instead */
	if (priv->test_mode) {
		g_autofree gchar *filename = NULL;
		close(priv->fd);
		filename = g_strdup_printf ("%s/remote/%s",
					    priv->fw_dir,
					    priv->aux_node);
		if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
			g_set_error (error,
			     	G_IO_ERROR,
			     	G_IO_ERROR_NOT_FOUND,
			     	"no device exists %s",
			     	filename);
			return FALSE;
		}
		priv->fd = open (filename, O_RDWR);
		if (priv->fd == -1) {
			g_set_error (error,
				     G_IO_ERROR,
				     g_io_error_from_errno (errno),
				     "cannot open device %s: %s",
				     filename, g_strerror (errno));
			return FALSE;
		}
		return TRUE;
	}

	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	if (synapticsmst_common_enable_remote_control (connection)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to enable MST remote control");
		return FALSE;
	} else {
		return TRUE;
	}
}

static gboolean
synapticsmst_device_disable_remote_control (SynapticsMSTDevice *device, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	/* in test mode we need to open a different file node instead */
	if (priv->test_mode) {
		g_autofree gchar *filename = NULL;
		close(priv->fd);
		filename = g_strdup_printf ("%s/%s",
					    priv->fw_dir,
					    priv->aux_node);
		if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
			g_set_error (error,
			     	G_IO_ERROR,
			     	G_IO_ERROR_NOT_FOUND,
			     	"no device exists %s",
			     	filename);
			return FALSE;
		}
		priv->fd = open (filename, O_RDWR);
		if (priv->fd == -1) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PERMISSION_DENIED,
				     "cannot open device %s",
				     filename);
			return FALSE;
		}
		return TRUE;
	}

	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	if (synapticsmst_common_disable_remote_control (connection)) {
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
synapticsmst_device_scan_cascade_device (SynapticsMSTDevice *device,
					 GError ** error,
					 guint8 tx_port)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint8 layer = priv->layer + 1;
	guint16 rad = priv->rad | (tx_port << (2 * (priv->layer)));
	guint8 byte[4];
	guint8 rc;
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	if (priv->test_mode)
		return TRUE;

	/* reset */
	priv->has_cascade = FALSE;

	if (!synapticsmst_device_enable_remote_control (device, error)) {
		g_prefix_error (error,
				"failed to scan cascade device on tx_port %d: ",
				tx_port);
		return FALSE;
	}

	connection = synapticsmst_common_new (priv->fd, layer, rad);
	rc = synapticsmst_common_read_dpcd (connection, REG_RC_CAP, (gint *)byte, 1);
	if (rc == DPCD_SUCCESS ) {
		if (byte[0] & 0x04) {
			synapticsmst_common_read_dpcd (connection, REG_VENDOR_ID, (gint *)byte, 3);
			if (byte[0] == 0x90 && byte[1] == 0xCC && byte[2] == 0x24)
				priv->has_cascade = TRUE;
		}
	}

	if (!synapticsmst_device_disable_remote_control (device, error)) {
		g_prefix_error (error,
				"failed to scan cascade device on tx_port %d: ",
				tx_port);
		return FALSE;
	}

	return TRUE;
}

static gboolean
synapticsmst_device_read_board_id (SynapticsMSTDevice *device,
				   SynapticsMSTConnection *connection,
				   guint8 *byte,
				   GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint8 rc;

	if (priv->test_mode) {
		g_autofree gchar *filename = NULL;
		gint fd;
		filename = g_strdup_printf ("%s/remote/%s_eeprom",
					    priv->fw_dir,
					    priv->aux_node);
		if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "no device exists %s",
				     filename);
			return FALSE;
		}
		fd = open (filename, O_RDONLY);
		if (fd == -1) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PERMISSION_DENIED,
				     "cannot open device %s",
				     filename);
			return FALSE;
		}
		if (read (fd, byte, 2) != 2) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "error reading EEPROM file %s",
				     filename);
			close (fd);
			return FALSE;
		}
		close (fd);
	} else {
		rc = synapticsmst_common_rc_get_command (connection,
							 UPDC_READ_FROM_EEPROM,
							 2, ADDR_CUSTOMER_ID, byte);
		if (rc) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read from EEPROM of device");
			return FALSE;
		}
	}
	return TRUE;
}


/*
 * Adds a GUID
 * - GUID is MST-$SYSTEMID-$BOARDID
 * - $BOARDID includes CUSTOMERID in first byte, BOARD in second byte
 */
static void
synapticsmst_create_guid (SynapticsMSTDevice *device,
			 const gchar *system)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	g_ptr_array_add (priv->guids, g_strdup_printf ("MST-%s-%u", system, priv->board_id));
}

static void
synapticsmst_create_dell_dock_guids (SynapticsMSTDevice *device,
				     const gchar *dock_type)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	const gchar *dell_docks[] = {"wd15", "tb16", "tb18", NULL};
	g_autofree gchar *chip_id_down = g_ascii_strdown (priv->chip_id_str, -1);

	for (guint i = 0; dell_docks[i] != NULL; i++) {
		g_autofree gchar *tmp = NULL;
		if (dock_type != NULL) {
			tmp = g_strdup_printf ("%s-%s", dock_type, chip_id_down);
			synapticsmst_create_guid (device, tmp);
			break;
		}
		tmp = g_strdup_printf ("%s-%s", dell_docks[i], chip_id_down);
		synapticsmst_create_guid (device, tmp);
	}
}

static gboolean
synapticsmst_create_guids (SynapticsMSTDevice *device,
			   const gchar *system_type,
			   GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->test_mode) {
		g_autofree gchar *tmp = NULL;
		tmp = g_strdup_printf ("test-%s", priv->chip_id_str);
		synapticsmst_create_guid (device, tmp);
		return TRUE;
	}

	switch (priv->board_id >> 8) {
	/* only dell is supported for today */
	case CUSTOMERID_DELL:
		/* If we know the dock from another plugin, use it, otherwise make GUIDs for all those we know about */
		if (priv->board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_WD15_TB16_WIRE ||
		    priv->board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_FUTURE)
			synapticsmst_create_dell_dock_guids (device, NULL);
		else if (priv->board_id == SYNAPTICSMST_DEVICE_BOARDID_DELL_WLD15_WIRELESS)
			synapticsmst_create_dell_dock_guids (device, "wld15");
		/* This is a host system, use system ID */
		else
			synapticsmst_create_guid (device, system_type);
		break;
	/* EVB development board */
	case 0:
		synapticsmst_create_guid (device, "evb");
		break;
	/* unknown */
	default:
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Unknown board_id %x",
			     priv->board_id);
		return FALSE;
	}

	return TRUE;
}

gboolean
synapticsmst_device_enumerate_device (SynapticsMSTDevice *device,
				      const gchar *system_type,
				      GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint8 byte[16];
	guint8 rc;
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	//FIXME?
	if (!synapticsmst_device_open (device, error)) {
		g_prefix_error (error, "Failed to open device in DP Aux Node %s: ",
				synapticsmst_device_get_aux_node (device));
		return FALSE;
	}

	/* enable remote control */
	if (!synapticsmst_device_enable_remote_control (device, error))
		return FALSE;

	/* read firmware version */
	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	rc = synapticsmst_common_read_dpcd (connection,
					    REG_FIRMWARE_VERSION,
					    (gint *)byte, 3);
	if (rc) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to read dpcd from device");
		goto error_disable_remote;
	}
	priv->version = g_strdup_printf ("%1d.%02d.%03d", byte[0], byte[1], byte[2]);

	/* read board ID */
	if (!synapticsmst_device_read_board_id (device, connection, byte, error))
		goto error_disable_remote;
	priv->board_id = (byte[0] << 8) | (byte[1]);

	/* read board chip_id */
	rc = synapticsmst_common_read_dpcd (connection,
					    REG_CHIP_ID,
					    (gint *)byte, 2);
	if (rc) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to read dpcd from device");
		goto error_disable_remote;
	}
	priv->chip_id = (byte[0] << 8) | (byte[1]);
	priv->chip_id_str = g_strdup_printf ("VMM%02x%02x", byte[0], byte[1]);
	if (!synapticsmst_create_guids (device, system_type, error))
		goto error_disable_remote;

	/* disable remote control */
	if (!synapticsmst_device_disable_remote_control (device, error))
		return FALSE;

	return TRUE;

error_disable_remote:
	synapticsmst_device_disable_remote_control (device, NULL);
	return FALSE;
}

const gchar *
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

static guint16
synapticsmst_device_get_chip_id (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->chip_id;
}

const gchar *
synapticsmst_device_get_chip_id_str (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->chip_id_str;
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

gboolean
synapticsmst_device_get_cascade (SynapticsMSTDevice *device)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	return priv->has_cascade;
}

static gboolean
synapticsmst_device_get_flash_checksum (SynapticsMSTDevice *device,
					gint length, gint offset,
					guint32 *checksum, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	if (synapticsmst_common_rc_special_get_command (connection,
							UPDC_CAL_EEPROM_CHECKSUM,
							length, offset,
							NULL, 4,
							(guint8 *)checksum)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Failed to get flash checksum");
		return FALSE;
	} else {
		return TRUE;
	}
}

static gboolean
synapticsmst_device_set_flash_sector_erase (SynapticsMSTDevice *device,
					    guint16 rc_cmd,
					    guint16 offset,
					    GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint16 us_data;
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	/* Need to add Wp control ? */
	us_data = rc_cmd + offset;

	if (synapticsmst_common_rc_set_command (connection,
						UPDC_FLASH_ERASE,
						2, 0, (guint8 *)&us_data)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "can't sector erase flash at offset %x", offset);
		return FALSE;
	}

	return TRUE;
}

static gboolean
synapticsmst_device_update_tesla_leaf_firmware (SynapticsMSTDevice *device,
						guint32 payload_len,
						guint8 *payload_data,
						GFileProgressCallback progress_cb,
						gpointer progress_data,
						GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(SynapticsMSTConnection) connection = NULL;
	guint32 data_to_write = 0;
	guint32 offset = 0;
	guint32 write_loops = 0;
	guint8 rc = 0;

	write_loops = (payload_len / BLOCK_UNIT);
	data_to_write = payload_len;

	if (payload_len % BLOCK_UNIT)
		write_loops++;

	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		guint32 checksum = 0;
		guint32 flash_checksum = 0;

		if (!synapticsmst_device_set_flash_sector_erase (device, 0xffff, 0, error))
			return FALSE;
		g_debug ("Waiting for flash clear to settle");
		g_usleep (5000000);

		for (guint32 i = 0; i < write_loops; i++) {
			guint8 length = BLOCK_UNIT;

			if (data_to_write < BLOCK_UNIT)
				length = data_to_write;
			rc = synapticsmst_common_rc_set_command (connection,
								 UPDC_WRITE_TO_EEPROM,
								 length, offset,
								 payload_data + offset);
			if (rc) {
				/* repeat once */
				rc = synapticsmst_common_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM,
									 length, offset,
									 payload_data + offset);
			}
			if (rc)
				break;

			offset += length;
			data_to_write -= length;
			if (progress_cb != NULL) {
				progress_cb ((goffset) i * 100,
					     (goffset) (write_loops -1) * 100,
					     progress_data);
			}
		}
		if (rc) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "can't write flash at offset 0x%04x",
				     offset);
			return FALSE;
		}

		/* check data just written */
		for (guint32 i = 0; i < payload_len; i++)
			checksum += *(payload_data + i);

		if (!synapticsmst_device_get_flash_checksum (device,
								payload_len,
								0,
								&flash_checksum,
								error)) {
			g_set_error_literal (error,
						G_IO_ERROR,
						G_IO_ERROR_INVALID_DATA,
						"Failed to read checksum");
			return FALSE;
		}
		if (checksum == flash_checksum)
			break;
		g_debug ("attempt %u: checksum %x didn't match %x", retries_cnt, flash_checksum, checksum);

		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					"checksum %x mismatched %x",
					flash_checksum,
					checksum);
			return FALSE;
		}
	}

	return TRUE;
}


static gboolean
synapticsmst_device_check_firmware_content (SynapticsMSTDevice *device,
					    GBytes *fw,
					    SynapticsMSTChipKind chip_type,
					    GError **error)
{
	guint8 *payload_data;
	gsize payload_len, payload_len_max;
	gint checksum = 0;
	guint32 offset = 0;
	guint32 code_size = 0;

	switch (chip_type) {
	case SYNAPTICSMST_CHIP_KIND_TESLA_LEAF:
		payload_len_max = PAYLOAD_SIZE_64K;
		break;
	default:
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "unknown chip type %u",
			     chip_type);
		return FALSE;

	}

	/* get firmware data and check size */
	payload_data = g_bytes_get_data (fw, &payload_len);
	if (payload_len > payload_len_max || payload_len == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "invalid payload size %" G_GSIZE_FORMAT "(max %" G_GSIZE_FORMAT")",
			     payload_len,
			     payload_len_max);
		return FALSE;
	}

	/* check firmware content */
	for (guint8 i = 0; i < 128; i++)
		checksum += *(payload_data + i);
	if (checksum & 0xff) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "EDID checksum error: %d",
			     checksum);
		return FALSE;
	}
	/* EDID */
	checksum = 0;
	offset = 128;
	for (guint8 i = 0; i < 128; i++)
		checksum += *(payload_data + offset + i);

	if (checksum & 0xff) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "EDID checksum error");
		return FALSE;
	}
	/* CFG 0 */
	checksum = 0;
	offset = 0x100;
	for (guint16 i = 0; i < 256; i++)
		checksum += *(payload_data + offset + i);

	if (checksum & 0xff) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "configuration checksum error");
		return FALSE;
	}
	/* CFG 1 */
	checksum = 0;
	offset = 0x200;
	for (guint16 i = 0; i < 256; i++)
		checksum += *(payload_data + offset + i);
	if (checksum & 0xff) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "configuration checksum error");
		return FALSE;
	}
	/* Firmware Size */
	checksum = 0;
	offset = 0x400;
	if (chip_type == SYNAPTICSMST_CHIP_KIND_TESLA_LEAF) {
		code_size = (*(payload_data + offset) << 8) + *(payload_data + offset + 1);
		if (code_size >= 0xffff) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "invalid firmware size");
			return FALSE;
		}
	}
	/* Firmware Checksum */
	if (chip_type == SYNAPTICSMST_CHIP_KIND_TESLA_LEAF) {
		for (guint32 i = 0; i < (code_size + 17); i++)
			checksum += *(payload_data + offset + i);

		if (checksum & 0xff) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "firmware checksum error");
			return FALSE;
		}
	}

	return TRUE;
}


static gboolean
synapticsmst_device_restart (SynapticsMSTDevice *device,
			     GError **error)
{
	g_autoptr(SynapticsMSTConnection) connection = NULL;
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint8 dwData[4] = {0xF5, 0, 0 ,0};

	/* issue the reboot command, ignore return code (triggers before returning) */
	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	synapticsmst_common_rc_set_command (connection,
					    UPDC_WRITE_TO_MEMORY,
					    4, (gint) 0x2000FC, (guint8*) &dwData);

	return TRUE;
}

gboolean
synapticsmst_device_write_firmware (SynapticsMSTDevice *device,
				    GBytes *fw,
				    GFileProgressCallback progress_cb,
				    gpointer progress_data,
				    GError **error)
{
	const guint8 *payload_data;
	gsize payload_len;
	guint16 tmp;
	SynapticsMSTChipKind chip_type = SYNAPTICSMST_CHIP_KIND_UNKNOWN;
	g_autoptr(FuDeviceLocker) locker = NULL;

	payload_data = g_bytes_get_data (fw, &payload_len);

	if (synapticsmst_device_get_chip_id (device) < 0x5000)
		chip_type = SYNAPTICSMST_CHIP_KIND_TESLA_LEAF;

	if (!synapticsmst_device_check_firmware_content (device, fw, chip_type, error)){
		g_prefix_error (error, "Invalid file content: ");
		return FALSE;
	}

	/* TODO: May need a way to override this to cover field
	 * issues of invalid firmware flashed*/
	/* check firmware and board ID again */
	tmp = (*(payload_data + ADDR_CUSTOMER_ID) << 8) + *(payload_data + ADDR_BOARD_ID);
	if (synapticsmst_device_get_board_id (device) >> 8 == 0) {
		g_warning ("EVB board detected, bypassing customer ID check");
	} else {
		if (tmp != synapticsmst_device_get_board_id (device)) {
			g_set_error_literal (error,
						G_IO_ERROR,
						G_IO_ERROR_INVALID_DATA,
						"board ID mismatch");
			return FALSE;
		}
	}
	/* open device */
	if (!synapticsmst_device_open (device, error)) {
		g_prefix_error (error,
				"can't open DP Aux node %s",
				synapticsmst_device_get_aux_node (device));
		return FALSE;
	}

	/* enable remote control and disable on exit */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) synapticsmst_device_enable_remote_control,
					    (FuDeviceLockerFunc) synapticsmst_device_restart,
					    error);
	if (locker == NULL)
		return FALSE;

	/* update firmware */
	if (!synapticsmst_device_update_tesla_leaf_firmware (device,
							     payload_len,
							     payload_data,
							     progress_cb,
							     progress_data,
							     error)) {
		g_prefix_error (error, "Firmware update failed: ");
		return FALSE;
	}

	return TRUE;
}

SynapticsMSTDevice *
synapticsmst_device_new (SynapticsMSTDeviceKind kind,
			 const gchar *aux_node,
			 guint8 layer,
			 guint16 rad)
{
	SynapticsMSTDevice *device;
	SynapticsMSTDevicePrivate *priv;

	device = g_object_new (SYNAPTICSMST_TYPE_DEVICE, NULL);
	priv = GET_PRIVATE (device);

	priv->aux_node = g_strdup(aux_node);
	priv->kind = kind;
	priv->version = NULL;
	priv->layer = layer;
	priv->rad = rad;
	priv->has_cascade = FALSE;

	return SYNAPTICSMST_DEVICE (device);
}

gboolean
synapticsmst_device_open (SynapticsMSTDevice *device, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	g_autofree gchar *filename = NULL;
	guint8 byte[4];
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	/* file doesn't exist on this system */
	filename = g_strdup_printf ("%s/%s", priv->fw_dir, priv->aux_node);
	if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "no device exists %s",
			     filename);
		return FALSE;
	}

	/* can't open aux node, try use sudo to get the permission */
	priv->fd = open (filename, O_RDWR);
	if (priv->fd == -1) {
		g_set_error (error,
			     G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "cannot open device %s: %s",
			     filename, g_strerror (errno));
		return FALSE;
	}

	connection = synapticsmst_common_new (priv->fd, 0, 0);
	if (synapticsmst_common_aux_node_read (connection, REG_RC_CAP, (gint *)byte, 1) == DPCD_SUCCESS) {
		if (byte[0] & 0x04) {
			synapticsmst_common_aux_node_read (connection,
							   REG_VENDOR_ID,
							   (gint *)byte, 3);
			if (byte[0] == 0x90 && byte[1] == 0xCC && byte[2] == 0x24)
				return TRUE;
		}
	}

	/* not a correct device */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "no device");
	close (priv->fd);
	priv->fd = 0;
	return FALSE;
}
