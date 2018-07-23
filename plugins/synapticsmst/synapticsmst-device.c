/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2018 Ryan Chang <ryan.chang@synaptics.com>
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

#define BIT( n ) ( 1 << (n) )
#define FLASH_SECTOR_ERASE_4K		0x1000
#define FLASH_SECTOR_ERASE_32K		0x2000
#define FLASH_SECTOR_ERASE_64K		0x3000
#define EEPROM_TAG_OFFSET		0x1FFF0
#define EEPROM_BANK_OFFSET		0x20000
#define EEPROM_ESM_OFFSET		0x40000
#define ESM_CODE_SIZE			0x40000
#define PAYLOAD_SIZE_512K		0x80000
#define PAYLOAD_SIZE_64K		0x10000
#define MAX_RETRY_COUNTS		10
#define BLOCK_UNIT			64
#define BANKTAG_0			0
#define BANKTAG_1			1
#define CRC_8				8
#define CRC_16				16
#define REG_ESM_DISABLE			0x2000fc
#define REG_QUAD_DISABLE		0x200fc0
#define REG_HDCP22_DISABLE		0x200f90

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
	if ((board_id & 0x3300) || (board_id & 0x5300))
		return "SYNA EVB board";
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

static gboolean
synapticsmst_device_get_active_bank_panamera (SynapticsMSTDevice *device,
					      guint8 *bank_out,
					      GError **error)
{
	g_autoptr(SynapticsMSTConnection) connection = NULL;
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint32 dwData[16];
	guint8 rc;

	/* get used bank */
	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
	rc = synapticsmst_common_rc_get_command (connection,
						 UPDC_READ_FROM_MEMORY,
						 ((sizeof(dwData)/sizeof(dwData[0]))*4),
						 (gint) 0x20010c, (guint8*) dwData);
	if (rc) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Get active bank failed");
		return FALSE;
	}
	if ((dwData[0] & BIT(7)) || (dwData[0] & BIT(30)))
		*bank_out = BANKTAG_1;
	else
		*bank_out = BANKTAG_0;

	g_debug ("bank in use:%x", *bank_out);

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
	guint8 bank;
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
	g_debug ("BoardID %x", priv->board_id);

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

	/* if running on panamera, check the active bank (for debugging logs) */
	if (priv->chip_id > 0x5000 &&
	   !synapticsmst_device_get_active_bank_panamera (device, &bank, error))
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
	}

	return TRUE;
}

static guint16
synapticsmst_device_get_crc (guint16 crc, guint8 type, guint32 length, guint8 *payload_data)
{
	static const guint16 CRC16_table[] = {
		0x0000, 0x8005, 0x800f, 0x000a, 0x801b, 0x001e, 0x0014, 0x8011, 0x8033, 0x0036, 0x003c, 0x8039, 0x0028, 0x802d, 0x8027, 0x0022,
		0x8063, 0x0066, 0x006c, 0x8069, 0x0078, 0x807d, 0x8077, 0x0072, 0x0050, 0x8055, 0x805f, 0x005a, 0x804b, 0x004e, 0x0044, 0x8041,
		0x80c3, 0x00c6, 0x00cc, 0x80c9, 0x00d8, 0x80dd, 0x80d7, 0x00d2, 0x00f0, 0x80f5, 0x80ff, 0x00fa, 0x80eb, 0x00ee, 0x00e4, 0x80e1,
		0x00a0, 0x80a5, 0x80af, 0x00aa, 0x80bb, 0x00be, 0x00b4, 0x80b1, 0x8093, 0x0096, 0x009c, 0x8099, 0x0088, 0x808d, 0x8087, 0x0082,
		0x8183, 0x0186, 0x018c, 0x8189, 0x0198, 0x819d, 0x8197, 0x0192, 0x01b0, 0x81b5, 0x81bf, 0x01ba, 0x81ab, 0x01ae, 0x01a4, 0x81a1,
		0x01e0, 0x81e5, 0x81ef, 0x01ea, 0x81fb, 0x01fe, 0x01f4, 0x81f1, 0x81d3, 0x01d6, 0x01dc, 0x81d9, 0x01c8, 0x81cd, 0x81c7, 0x01c2,
		0x0140, 0x8145, 0x814f, 0x014a, 0x815b, 0x015e, 0x0154, 0x8151, 0x8173, 0x0176, 0x017c, 0x8179, 0x0168, 0x816d, 0x8167, 0x0162,
		0x8123, 0x0126, 0x012c, 0x8129, 0x0138, 0x813d, 0x8137, 0x0132, 0x0110, 0x8115, 0x811f, 0x011a, 0x810b, 0x010e, 0x0104, 0x8101,
		0x8303, 0x0306, 0x030c, 0x8309, 0x0318, 0x831d, 0x8317, 0x0312, 0x0330, 0x8335, 0x833f, 0x033a, 0x832b, 0x032e, 0x0324, 0x8321,
		0x0360, 0x8365, 0x836f, 0x036a, 0x837b, 0x037e, 0x0374, 0x8371, 0x8353, 0x0356, 0x035c, 0x8359, 0x0348, 0x834d, 0x8347, 0x0342,
		0x03c0, 0x83c5, 0x83cf, 0x03ca, 0x83db, 0x03de, 0x03d4, 0x83d1, 0x83f3, 0x03f6, 0x03fc, 0x83f9, 0x03e8, 0x83ed, 0x83e7, 0x03e2,
		0x83a3, 0x03a6, 0x03ac, 0x83a9, 0x03b8, 0x83bd, 0x83b7, 0x03b2, 0x0390, 0x8395, 0x839f, 0x039a, 0x838b, 0x038e, 0x0384, 0x8381,
		0x0280, 0x8285, 0x828f, 0x028a, 0x829b, 0x029e, 0x0294, 0x8291, 0x82b3, 0x02b6, 0x02bc, 0x82b9, 0x02a8, 0x82ad, 0x82a7, 0x02a2,
		0x82e3, 0x02e6, 0x02ec, 0x82e9, 0x02f8, 0x82fd, 0x82f7, 0x02f2, 0x02d0, 0x82d5, 0x82df, 0x02da, 0x82cb, 0x02ce, 0x02c4, 0x82c1,
		0x8243, 0x0246, 0x024c, 0x8249, 0x0258, 0x825d, 0x8257, 0x0252, 0x0270, 0x8275, 0x827f, 0x027a, 0x826b, 0x026e, 0x0264, 0x8261,
		0x0220, 0x8225, 0x822f, 0x022a, 0x823b, 0x023e, 0x0234, 0x8231, 0x8213, 0x0216, 0x021c, 0x8219, 0x0208, 0x820d, 0x8207, 0x0202
	};
	static const guint16 CRC8_table[] = {
		0x00, 0xd5, 0x7f, 0xaa, 0xfe, 0x2b, 0x81, 0x54, 0x29, 0xfc, 0x56, 0x83, 0xd7, 0x02, 0xa8, 0x7d,
		0x52, 0x87, 0x2d, 0xf8, 0xac, 0x79, 0xd3, 0x06, 0x7b, 0xae, 0x04, 0xd1, 0x85, 0x50, 0xfa, 0x2f,
		0xa4, 0x71, 0xdb, 0x0e, 0x5a, 0x8f, 0x25, 0xf0, 0x8d, 0x58, 0xf2, 0x27, 0x73, 0xa6, 0x0c, 0xd9,
		0xf6, 0x23, 0x89, 0x5c, 0x08, 0xdd, 0x77, 0xa2, 0xdf, 0x0a, 0xa0, 0x75, 0x21, 0xf4, 0x5e, 0x8b,
		0x9d, 0x48, 0xe2, 0x37, 0x63, 0xb6, 0x1c, 0xc9, 0xb4, 0x61, 0xcb, 0x1e, 0x4a, 0x9f, 0x35, 0xe0,
		0xcf, 0x1a, 0xb0, 0x65, 0x31, 0xe4, 0x4e, 0x9b, 0xe6, 0x33, 0x99, 0x4c, 0x18, 0xcd, 0x67, 0xb2,
		0x39, 0xec, 0x46, 0x93, 0xc7, 0x12, 0xb8, 0x6d, 0x10, 0xc5, 0x6f, 0xba, 0xee, 0x3b, 0x91, 0x44,
		0x6b, 0xbe, 0x14, 0xc1, 0x95, 0x40, 0xea, 0x3f, 0x42, 0x97, 0x3d, 0xe8, 0xbc, 0x69, 0xc3, 0x16,
		0xef, 0x3a, 0x90, 0x45, 0x11, 0xc4, 0x6e, 0xbb, 0xc6, 0x13, 0xb9, 0x6c, 0x38, 0xed, 0x47, 0x92,
		0xbd, 0x68, 0xc2, 0x17, 0x43, 0x96, 0x3c, 0xe9, 0x94, 0x41, 0xeb, 0x3e, 0x6a, 0xbf, 0x15, 0xc0,
		0x4b, 0x9e, 0x34, 0xe1, 0xb5, 0x60, 0xca, 0x1f, 0x62, 0xb7, 0x1d, 0xc8, 0x9c, 0x49, 0xe3, 0x36,
		0x19, 0xcc, 0x66, 0xb3, 0xe7, 0x32, 0x98, 0x4d, 0x30, 0xe5, 0x4f, 0x9a, 0xce, 0x1b, 0xb1, 0x64,
		0x72, 0xa7, 0x0d, 0xd8, 0x8c, 0x59, 0xf3, 0x26, 0x5b, 0x8e, 0x24, 0xf1, 0xa5, 0x70, 0xda, 0x0f,
		0x20, 0xf5, 0x5f, 0x8a, 0xde, 0x0b, 0xa1, 0x74, 0x09, 0xdc, 0x76, 0xa3, 0xf7, 0x22, 0x88, 0x5d,
		0xd6, 0x03, 0xa9, 0x7c, 0x28, 0xfd, 0x57, 0x82, 0xff, 0x2a, 0x80, 0x55, 0x01, 0xd4, 0x7e, 0xab,
		0x84, 0x51, 0xfb, 0x2e, 0x7a, 0xaf, 0x05, 0xd0, 0xad, 0x78, 0xd2, 0x07, 0x53, 0x86, 0x2c, 0xf9
	};
	guint8 val;
	guint16 remainder = (guint16) crc;
	guint8 *message = payload_data;

	if (type == CRC_8) {
		for (guint32 byte = 0; byte < length; ++byte) {
			val = (guint8)(message[byte] ^ remainder);
			remainder = CRC8_table[val];
		}
	} else {
		for (guint32 byte = 0; byte < length; ++byte) {
			val = (guint8)(message[byte] ^ (remainder >> 8));
			remainder = CRC16_table[val] ^ (remainder << 8);
		}
	}

	return remainder;
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
synapticsmst_device_update_esm (SynapticsMSTDevice *device,
				guint8 *payload_data,
				GFileProgressCallback progress_cb,
				gpointer progress_data,
				GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint32 checksum = 0;
	guint32 esm_sz = ESM_CODE_SIZE;
	guint32 flash_checksum = 0;
	guint32 unit_sz = BLOCK_UNIT;
	guint32 write_loops = 0;
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);

	for (guint32 i = 0; i < esm_sz; i++)
		checksum += *(payload_data + EEPROM_ESM_OFFSET +i);
	if (!synapticsmst_device_get_flash_checksum (device,
						    esm_sz,
						    EEPROM_ESM_OFFSET,
						    &flash_checksum, error)) {
		return FALSE;
	}

	/* ESM checksum same */
	if (checksum == flash_checksum) {
		g_debug ("ESM checksum already matches");
		return TRUE;
	}
	g_debug ("ESM checksum %x doesn't match expected %x", flash_checksum, checksum);

	/* erase ESM firmware */
	for (guint32 i = 0; i < 4; i++)	{
		if (!synapticsmst_device_set_flash_sector_erase (device, FLASH_SECTOR_ERASE_64K, i + 4, error)) {
			g_prefix_error (error, "Failed to erase sector %u: ", i);
			return FALSE;
		}
	}

	/* update ESM firmware */
	write_loops = (esm_sz / unit_sz);
	if (esm_sz % unit_sz)
		write_loops++;
	for (guint retries_cnt = 0; ; retries_cnt++) {
		guint32 write_idx = 0;
		guint32 write_offset = EEPROM_ESM_OFFSET;
		guint8 *esm_code_ptr = &payload_data[EEPROM_ESM_OFFSET];
		for (guint32 i = 0; i < write_loops; i++) {
			guint8 rc;
			rc = synapticsmst_common_rc_set_command (connection,
								 UPDC_WRITE_TO_EEPROM,
								 unit_sz,
								 write_offset,
								 esm_code_ptr + write_idx);
			if (rc) {
				/* repeat once */
				rc = synapticsmst_common_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM,
									 unit_sz,
									 write_offset,
									 esm_code_ptr + write_idx);
			}
			if (rc) {
				g_debug ("attempt %u: ESM update failed", retries_cnt);
				break;
			}
			write_offset += unit_sz;
			write_idx += unit_sz;
			if (progress_cb != NULL) {
				progress_cb ((goffset) i * 100,
					(goffset) (write_loops -1) * 100,
					progress_data);
			}

		}

		/* check ESM checksum */
		checksum = 0;
		flash_checksum = 0;
		for (guint32 i = 0; i < esm_sz; i++)
			checksum += *(payload_data + EEPROM_ESM_OFFSET +i);
		if (!synapticsmst_device_get_flash_checksum (device,
							     esm_sz,
							     EEPROM_ESM_OFFSET,
							     &flash_checksum,
							     error))
			return FALSE;

		/* ESM update done */
		if (checksum == flash_checksum)
			break;
		g_debug ("attempt %u: ESM checksum %x didn't match %x", retries_cnt, flash_checksum, checksum);

		/* abort */
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "ESM update failed after %u tries", retries_cnt);
			return FALSE;
		}
	}
	g_debug ("ESM successfully written");

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
synapticsmst_device_update_panamera_firmware (SynapticsMSTDevice *device,
					      guint32 payload_len,
					      guint8 *payload_data,
					      GFileProgressCallback progress_cb,
					      gpointer progress_data,
					      GError **error)
{

	guint16 crc_tmp = 0;
	guint32 fw_size;
	guint32 unit_sz = BLOCK_UNIT;
	guint32 write_loops = 0;
	guint8 bank_in_use;
	guint8 bank_to_update = BANKTAG_1;
	guint8 rc;
	guint8 readBuf[256];
	guint8 tagData[16];
	struct tm *pTM;
	time_t timeptr;
	g_autoptr(SynapticsMSTConnection) connection = NULL;
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);

	/* get used bank */
	if (!synapticsmst_device_get_active_bank_panamera (device, &bank_in_use, error))
		return FALSE;
	if (bank_in_use == BANKTAG_1)
		bank_to_update = BANKTAG_0;
	g_debug ("bank to update:%x", bank_to_update);

	/* get firmware size */
	fw_size =  0x410 + (*(payload_data + 0x400) << 24)
			 + (*(payload_data + 0x401) << 16)
			 + (*(payload_data + 0x402) << 8)
			 + (*(payload_data + 0x403));

	/* Current max firmware size is 104K */
	if (fw_size < payload_len)
		fw_size = 104 * 1024;
	g_debug ("Calculated fw size as %u", fw_size);

	/* Update firmware */
	write_loops = fw_size / unit_sz;
	if (fw_size % unit_sz)
		write_loops++;

	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		guint32 checksum = 0;
		guint32 erase_offset;
		guint32 flash_checksum = 0;
		guint32 write_idx;
		guint32 write_offset;

		/* erase storage */
		erase_offset = bank_to_update * 2;
		if (!synapticsmst_device_set_flash_sector_erase (device,
								 FLASH_SECTOR_ERASE_64K, erase_offset++, error))
			return FALSE;
		if (!synapticsmst_device_set_flash_sector_erase (device,
								 FLASH_SECTOR_ERASE_64K, erase_offset, error))
			return FALSE;
		g_debug ("Waiting for flash clear to settle");
		g_usleep (5000000);

		/* write */
		write_idx = 0;
		write_offset = EEPROM_BANK_OFFSET * bank_to_update;
		connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);
		for (guint32 i = 0; i < write_loops ; i++ ) {
			rc = synapticsmst_common_rc_set_command (connection,
								UPDC_WRITE_TO_EEPROM,
								unit_sz,
								write_offset,
								payload_data + write_idx);
			if (rc) {
				/* repeat once */
				rc = synapticsmst_common_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM,
									 unit_sz,
									 write_offset,
									 payload_data + write_idx);
			}
			if (rc)	{
				g_set_error_literal (error,
						     G_IO_ERROR,
						     G_IO_ERROR_INVALID_DATA,
						     "FW write fail");
				return FALSE;
			}

			write_offset += unit_sz;
			write_idx += unit_sz;
			if (progress_cb != NULL) {
				progress_cb ((goffset) i * 100,
					     (goffset) (write_loops -1) * 100,
					     progress_data);
			}
		}

		/* verify CRC */
		checksum = synapticsmst_device_get_crc ( 0, 16, fw_size, payload_data );
		for (guint32 i = 0; i < 4; i++) {
			g_usleep (1000);	/* wait crc calculation */
			if (synapticsmst_common_rc_special_get_command (connection,
									UPDC_CAL_EEPROM_CHECK_CRC16,
									fw_size, (EEPROM_BANK_OFFSET * bank_to_update),
									NULL, 4, (guint8 *)(&flash_checksum))) {
				g_set_error_literal (error,
						     G_IO_ERROR,
						     G_IO_ERROR_INVALID_DATA,
						     "Failed to get flash checksum");
				return FALSE;
			}
		}
		if (checksum == flash_checksum)
			break;
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "firmware update fail");
			return FALSE;
		}
		g_usleep (2000);
	}

	/* set tag valid */
	time (&timeptr);
	pTM = localtime (&timeptr);
	memset (tagData, 0, sizeof (tagData));
	memset (readBuf, 0, sizeof (readBuf));

	tagData[1] = pTM->tm_mon + 1;
	tagData[2] = pTM->tm_mday;
	tagData[3] = pTM->tm_year + 1900 - 2000;
	crc_tmp = synapticsmst_device_get_crc (0, 16, fw_size, payload_data);
	tagData[0] = bank_to_update;
	tagData[4] = (crc_tmp >> 8) & 0xff;
	tagData[5] = crc_tmp & 0xff;
	tagData[15] = (guint8) synapticsmst_device_get_crc (0, 8, 15, tagData);
	g_debug ("tag date %x %x %x crc %x %x %x %x", tagData[1], tagData[2], tagData[3], tagData[0], tagData[4], tagData[5], tagData[15]);

	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		gboolean match = TRUE;
		rc = synapticsmst_common_rc_set_command (connection,
							 UPDC_WRITE_TO_EEPROM,
							 16,
							 (EEPROM_BANK_OFFSET * bank_to_update + EEPROM_TAG_OFFSET),
							 tagData);
		if (rc) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to write TAG");
			return FALSE;
		}
		g_usleep (200);
		rc = synapticsmst_common_rc_get_command (connection,
							 UPDC_READ_FROM_EEPROM,
							 16,
							 (EEPROM_BANK_OFFSET * bank_to_update + EEPROM_TAG_OFFSET),
							 readBuf);
		if (rc) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read Tag");
			return FALSE;
		}
		for (guint32 i = 0; i < 16; i++){
			if (readBuf[i] != tagData[i]){
				match = FALSE;
				break;
			}
		}
		if (match)
			break;
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "set tag valid fail");
			return FALSE;
		}
	}

	/* set tag invalid*/
	rc = synapticsmst_common_rc_get_command (connection,
						 UPDC_READ_FROM_EEPROM, 1,
						 (EEPROM_BANK_OFFSET * bank_in_use + EEPROM_TAG_OFFSET + 15),
						 tagData);
	if (rc) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				    "Failed to read Tag from flash");
		return FALSE;
	}

	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		/* CRC8 is not 0xff, erase last 4k of bank# */
		if (tagData[0] != 0xff)	{
			guint32 erase_offset;
			/* offset for last 4k of bank# */
			erase_offset = (EEPROM_BANK_OFFSET * bank_in_use + EEPROM_BANK_OFFSET - 0x1000) / 0x1000;
			if (!synapticsmst_device_set_flash_sector_erase (device,
									 FLASH_SECTOR_ERASE_4K,
									 erase_offset,
									 error))
				return FALSE;
		/* CRC8 is 0xff, set it to 0x00 */
		} else {
			tagData[1] = 0x00;
			rc = synapticsmst_common_rc_set_command (connection,
								 UPDC_WRITE_TO_EEPROM, 1,
								 (EEPROM_BANK_OFFSET * bank_in_use + EEPROM_TAG_OFFSET + 15),
								 &tagData[1]);
		}

		rc = synapticsmst_common_rc_get_command (connection,
							 UPDC_READ_FROM_EEPROM, 1,
							 (EEPROM_BANK_OFFSET * bank_in_use + EEPROM_TAG_OFFSET + 15),
							 readBuf);
		if (rc) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "Failed to read CRC from flash");
			return FALSE;
		}
		if ((readBuf[0] == 0xff && tagData[0] != 0xff) ||
		    (readBuf[0] == 0x00 && tagData[0] == 0xff)) {
			break;
		}
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "set tag invalid fail");
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
	case SYNAPTICSMST_CHIP_KIND_PANAMERA:
		payload_len_max = PAYLOAD_SIZE_512K;
		break;
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
	if (chip_type == SYNAPTICSMST_CHIP_KIND_PANAMERA) {
		code_size = (*(payload_data + offset) << 24);
		code_size += (*(payload_data + offset + 1) << 16);
		code_size += (*(payload_data + offset + 2) << 8);
		code_size += (*(payload_data + offset + 3));
		if (code_size >= 0x02ffff) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "invalid firmware size");
			return FALSE;
		}
	} else {
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
	if (chip_type == SYNAPTICSMST_CHIP_KIND_PANAMERA) {
		guint32 i = 0;
		for ( i = 0; i < code_size + 1; i++ ) {
			checksum += *(payload_data + offset + 0x10 + i);
		}
		if ((checksum & 0xffff)!=((*(payload_data + 0x408) << 8) | (*(payload_data + 0x409)))) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "firmware checksum error");
			return FALSE;
		}
		checksum = 0;
		offset = EEPROM_ESM_OFFSET;
		for (i = 0; i < (payload_len - EEPROM_ESM_OFFSET); i++) {
			checksum += *(payload_data + offset + i);
		}
		if ((checksum & 0xffff)!=((*(payload_data + 0x40A) << 8) | (*(payload_data + 0x40B)))) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "ESM firmware checksum error");
			return FALSE;
		}
	} else {
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
synapticsmst_device_panamera_prepare_write (SynapticsMSTDevice *device, GError **error)
{
	SynapticsMSTDevicePrivate *priv = GET_PRIVATE (device);
	guint32 dwData[4] = {0};
	g_autoptr(SynapticsMSTConnection) connection = NULL;

	/* Need to detect flash mode and ESM first ? */
	/* disable flash Quad mode and ESM/HDCP2.2*/
	connection = synapticsmst_common_new (priv->fd, priv->layer, priv->rad);

	/* disable ESM first */
	dwData[0] = 0x21;
	if (synapticsmst_common_rc_set_command (connection,
						UPDC_WRITE_TO_MEMORY,
						4, (gint)REG_ESM_DISABLE, (guint8*)dwData)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "ESM disable failed");
		return FALSE;
	}

	/* wait for ESM exit */
	g_usleep (200);

	/* disable QUAD mode */
	if (synapticsmst_common_rc_get_command (connection,
						UPDC_READ_FROM_MEMORY,
						((sizeof(dwData)/sizeof(dwData[0]))*4),
						(gint)REG_QUAD_DISABLE, (guint8*)dwData)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Quad query failed");
		return FALSE;
	}

	dwData[0] = 0x00;
	if (synapticsmst_common_rc_set_command (connection,
						UPDC_WRITE_TO_MEMORY,
						4, (gint)REG_QUAD_DISABLE, (guint8*)dwData)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Quad disable failed");
		return FALSE;
	}

	/* disable HDCP2.2 */
	if (synapticsmst_common_rc_get_command (connection,
						UPDC_READ_FROM_MEMORY,
						4, (gint)REG_HDCP22_DISABLE, (guint8*)dwData)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "HDCP query failed");
		return FALSE;
	}

	dwData[0] = dwData[0] & (~BIT(2));
	if (synapticsmst_common_rc_set_command (connection,
						UPDC_WRITE_TO_MEMORY,
						4, (gint)REG_HDCP22_DISABLE, (guint8*)dwData)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "HDCP disable failed");
		return FALSE;
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

	if (synapticsmst_device_get_chip_id (device) > 0x5000)
		chip_type = SYNAPTICSMST_CHIP_KIND_PANAMERA;
	else
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
	if (chip_type == SYNAPTICSMST_CHIP_KIND_PANAMERA) {
		if (!synapticsmst_device_panamera_prepare_write (device, error)) {
			g_prefix_error (error, "Failed to prepare for write: ");
			return FALSE;
		}
		if (!synapticsmst_device_update_esm (device,
						     payload_data,
						     progress_cb,
						     progress_data,
						     error)) {
			g_prefix_error (error, "ESM update failed: ");
			return FALSE;
		}
		if (!synapticsmst_device_update_panamera_firmware (device,
								   payload_len,
								   payload_data,
								   progress_cb,
								   progress_data,
								   error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
	} else {
		if (!synapticsmst_device_update_tesla_leaf_firmware (device,
								     payload_len,
								     payload_data,
								     progress_cb,
								     progress_data,
								     error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
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
