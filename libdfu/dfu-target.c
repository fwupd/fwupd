/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:dfu-target
 * @short_description: Object representing a DFU-capable target
 *
 * This object allows uploading and downloading an image onto a
 * specific DFU-capable target.
 *
 * You only need to use this in preference to #DfuDevice if you only
 * want to update one target on the device. Most users will want to
 * update all the targets on the device at the same time.
 *
 * See also: #DfuDevice, #DfuImage
 */

#include "config.h"

#include <string.h>
#include <math.h>

#include "dfu-common.h"
#include "dfu-device-private.h"
#include "dfu-error.h"
#include "dfu-sector-private.h"
#include "dfu-target-private.h"

static void dfu_target_finalize			 (GObject *object);

typedef enum {
	DFU_CMD_DFUSE_GET_COMMAND		= 0x00,
	DFU_CMD_DFUSE_SET_ADDRESS_POINTER	= 0x21,
	DFU_CMD_DFUSE_ERASE			= 0x41,
	DFU_CMD_DFUSE_READ_UNPROTECT		= 0x92,
	DFU_CMD_DFUSE_LAST
} DfuCmdDfuse;

/**
 * DfuTargetPrivate:
 *
 * Private #DfuTarget data
 **/
typedef struct {
	DfuDevice		*device;		/* not refcounted */
	DfuCipherKind		 cipher_kind;
	gboolean		 done_setup;
	guint8			 alt_setting;
	guint8			 alt_idx;
	gchar			*alt_name;
	GPtrArray		*sectors;		/* of DfuSector */
	GHashTable		*sectors_erased;	/* of DfuSector:1 */
} DfuTargetPrivate;

enum {
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (DfuTarget, dfu_target, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_target_get_instance_private (o))

/**
 * dfu_target_class_init:
 **/
static void
dfu_target_class_init (DfuTargetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/**
	 * DfuTarget::percentage-changed:
	 * @device: the #DfuTarget instance that emitted the signal
	 * @percentage: the new percentage
	 *
	 * The ::percentage-changed signal is emitted when the percentage changes.
	 *
	 * Since: 0.5.4
	 **/
	signals [SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuTargetClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	object_class->finalize = dfu_target_finalize;
}

/**
 * dfu_target_init:
 **/
static void
dfu_target_init (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	priv->sectors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->sectors_erased = g_hash_table_new (g_direct_hash, g_direct_equal);
}

/**
 * dfu_target_finalize:
 **/
static void
dfu_target_finalize (GObject *object)
{
	DfuTarget *target = DFU_TARGET (object);
	DfuTargetPrivate *priv = GET_PRIVATE (target);

	g_free (priv->alt_name);
	g_ptr_array_unref (priv->sectors);
	g_hash_table_unref (priv->sectors_erased);

	/* we no longer care */
	if (priv->device != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (priv->device),
					      (gpointer *) &priv->device);
	}

	G_OBJECT_CLASS (dfu_target_parent_class)->finalize (object);
}

/**
 * dfu_target_sectors_to_string:
 **/
static gchar *
dfu_target_sectors_to_string (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	GString *str;
	guint i;

	str = g_string_new ("");
	for (i = 0; i < priv->sectors->len; i++) {
		g_autofree gchar *tmp = NULL;
		sector = g_ptr_array_index (priv->sectors, i);
		tmp = dfu_sector_to_string (sector);
		g_string_append_printf (str, "%s\n", tmp);
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * dfu_target_get_sector_for_addr:
 *
 * Returns: the sector that should be used for a specific address, or %NULL
 **/
static DfuSector *
dfu_target_get_sector_for_addr (DfuTarget *target, guint32 addr)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	guint i;

	for (i = 0; i < priv->sectors->len; i++) {
		sector = g_ptr_array_index (priv->sectors, i);
		if (addr < dfu_sector_get_address (sector))
			continue;
		if (addr > dfu_sector_get_address (sector) +
				dfu_sector_get_size (sector))
			continue;
		return sector;
	}
	return NULL;
}

/**
 * dfu_target_parse_sector:
 *
 * Parse the DfuSe sector format according to UM0424
 **/
static gboolean
dfu_target_parse_sector (DfuTarget *target,
			 const gchar *dfuse_sector_id,
			 guint32 addr,
			 guint zone,
			 guint number,
			 GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSectorCap cap = DFU_SECTOR_CAP_NONE;
	gchar *tmp;
	guint32 addr_offset = 0;
	guint64 nr_sectors;
	guint64 sector_size;
	guint i;

	/* parse # of sectors */
	nr_sectors = g_ascii_strtoull (dfuse_sector_id, &tmp, 10);
	if (nr_sectors > 999) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "Invalid number of sectors: %s",
			     dfuse_sector_id);
		return FALSE;
	}

	/* check this is the delimiter */
	if (tmp[0] != '*') {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "Invalid sector ID: %s",
			     dfuse_sector_id);
		return FALSE;
	}

	/* parse sector size */
	sector_size = g_ascii_strtoull (tmp + 1, &tmp, 10);
	if (sector_size > 999) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "Invalid sector size: %s",
			     dfuse_sector_id);
		return FALSE;
	}

	/* optional spaces */
	while (tmp[0] == ' ')
		tmp++;

	/* get multiplier */
	switch (tmp[0]) {
	case 'B':		/* byte */
		break;
	case 'K':		/* Kilo */
		sector_size *= 0x400;
		break;
	case 'M':		/* Mega */
		sector_size *= 0x100000	;
		break;
	default:
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "Invalid sector multiplier: %s",
			     tmp);
		return FALSE;
	}

	/* get sector type */
	switch (tmp[1]) {
	case 'a':
		cap = DFU_SECTOR_CAP_READABLE;
		break;
	case 'b':
		cap = DFU_SECTOR_CAP_ERASEABLE;
		break;
	case 'c':
		cap = DFU_SECTOR_CAP_READABLE |
		      DFU_SECTOR_CAP_ERASEABLE;
		break;
	case 'd':
		cap = DFU_SECTOR_CAP_WRITEABLE;
		break;
	case 'e':
		cap = DFU_SECTOR_CAP_READABLE |
		      DFU_SECTOR_CAP_WRITEABLE;
		break;
	case 'f':
		cap = DFU_SECTOR_CAP_ERASEABLE |
		      DFU_SECTOR_CAP_WRITEABLE;
		break;
	case 'g':
		cap = DFU_SECTOR_CAP_READABLE |
		      DFU_SECTOR_CAP_ERASEABLE |
		      DFU_SECTOR_CAP_WRITEABLE;
		break;
	default:
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "Invalid sector type: %s",
			     tmp);
		return FALSE;
	}

	/* add all the sectors */
	for (i = 0; i < nr_sectors; i++) {
		DfuSector *sector;
		sector = dfu_sector_new (addr + addr_offset,
					 sector_size,
					 (nr_sectors * sector_size)- addr_offset,
					 zone,
					 number,
					 cap);
		g_ptr_array_add (priv->sectors, sector);
		addr_offset += dfu_sector_get_size (sector);
	}
	return TRUE;
}

/**
 * dfu_target_parse_sectors: (skip)
 *
 * Parse the DfuSe format according to UM0424
 **/
gboolean
dfu_target_parse_sectors (DfuTarget *target, const gchar *alt_name, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	guint64 addr;
	guint i;
	guint j;
	g_autofree gchar *str_debug = NULL;
	g_auto(GStrv) zones = NULL;

	/* not set */
	if (alt_name == NULL)
		return TRUE;

	/* do we have any hint for the cipher */
	if (g_strstr_len (alt_name, -1, "|XTEA") != NULL)
		priv->cipher_kind = DFU_CIPHER_KIND_XTEA;

	/* From the Neo Freerunner */
	if (g_str_has_prefix (alt_name, "RAM 0x")) {
		DfuSector *sector;
		addr = g_ascii_strtoull (alt_name + 6, NULL, 16);
		if (addr == 0 && addr > G_MAXUINT32)
			return FALSE;
		g_debug ("RAM descripton, so parsing");
		sector = dfu_sector_new (addr, /* addr */
					 0x0, /* size */
					 0x0, /* size_left */
					 0x0, /* zone */
					 0x0, /* number */
					 DFU_SECTOR_CAP_READABLE |
					 DFU_SECTOR_CAP_WRITEABLE);
		g_ptr_array_add (priv->sectors, sector);
	}

	/* not a DfuSe alternative name */
	if (alt_name[0] != '@')
		return TRUE;

	/* clear any existing zones */
	g_ptr_array_set_size (priv->sectors, 0);

	/* parse zones */
	zones = g_strsplit (alt_name, "/", -1);
	g_debug ("DfuSe nice alt-name: %s", g_strchomp (zones[0] + 1));
	for (i = 1; zones[i] != NULL; i += 2) {
		g_auto(GStrv) sectors = NULL;

		/* parse address */
		if (!g_str_has_prefix (zones[i], "0x"))
			return FALSE;
		addr = g_ascii_strtoull (zones[i] + 2, NULL, 16);
		if (addr > G_MAXUINT32)
			return FALSE;

		/* no sectors?! */
		if (zones[i+1] == NULL) {
			g_set_error_literal (error,
					     DFU_ERROR,
					     DFU_ERROR_NOT_SUPPORTED,
					     "No sector section");
			return FALSE;
		}

		/* parse sectors */
		sectors = g_strsplit (zones[i+1], ",", -1);
		for (j = 0; sectors[j] != NULL; j++) {
			if (!dfu_target_parse_sector (target,
						      sectors[j],
						      addr,
						      (i - 1) / 2, j,
						      error))
				return FALSE;
		}
	}

	/* success */
	str_debug = dfu_target_sectors_to_string (target);
	g_debug ("%s", str_debug);
	return TRUE;
}

/**
 * dfu_target_new: (skip)
 * @device: a #DfuDevice
 * @iface: a #GUsbInterface
 *
 * Creates a new DFU target, which represents an alt-setting on a
 * DFU-capable device.
 *
 * Return value: a #DfuTarget, or %NULL if @iface was not DFU-capable
 *
 * Since: 0.5.4
 **/
DfuTarget *
dfu_target_new (DfuDevice *device, GUsbInterface *iface)
{
	DfuTargetPrivate *priv;
	DfuTarget *target;
	target = g_object_new (DFU_TYPE_TARGET, NULL);
	priv = GET_PRIVATE (target);
	priv->device = device;
	priv->alt_idx = g_usb_interface_get_index (iface);
	priv->alt_setting = g_usb_interface_get_alternate (iface);

	/* if we try to ref the target and destroy the device */
	g_object_add_weak_pointer (G_OBJECT (priv->device),
				   (gpointer *) &priv->device);

	return target;
}

/**
 * dfu_target_get_sectors:
 * @target: a #GUsbDevice
 *
 * Gets the sectors exported by the device.
 *
 * Return value: (transfer none) (element-type DfuSector): sectors
 *
 * Since: 0.5.4
 **/
GPtrArray *
dfu_target_get_sectors (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	return priv->sectors;
}

/**
 * dfu_target_status_to_error_msg:
 * @status: a #DfuStatus, e.g. %DFU_STATUS_ERR_ERASE
 *
 * Converts an enumerated value to an error description.
 *
 * Return value: a string
 *
 * Since: 0.5.4
 **/
static const gchar *
dfu_target_status_to_error_msg (DfuStatus status)
{
	if (status == DFU_STATUS_OK)
		return "No error condition is present";
	if (status == DFU_STATUS_ERR_TARGET)
		return "Firmware is not for designed this device";
	if (status == DFU_STATUS_ERR_FILE)
		return "Firmware is for this device but fails verification";
	if (status == DFU_STATUS_ERR_WRITE)
		return "Device is unable to write memory";
	if (status == DFU_STATUS_ERR_ERASE)
		return "Memory erase function failed";
	if (status == DFU_STATUS_ERR_CHECK_ERASED)
		return "Memory erase check failed";
	if (status == DFU_STATUS_ERR_PROG)
		return "Program memory function failed";
	if (status == DFU_STATUS_ERR_VERIFY)
		return "Programmed memory failed verification";
	if (status == DFU_STATUS_ERR_ADDRESS)
		return "Cannot program memory due to address out of range";
	if (status == DFU_STATUS_ERR_NOTDONE)
		return "Received zero-length download but data is incomplete";
	if (status == DFU_STATUS_ERR_FIRMWARE)
		return "Device firmware is corrupt";
	if (status == DFU_STATUS_ERR_VENDOR)
		return "Vendor-specific error";
	if (status == DFU_STATUS_ERR_USBR)
		return "Device detected unexpected USB reset signaling";
	if (status == DFU_STATUS_ERR_POR)
		return "Device detected unexpected power on reset";
	if (status == DFU_STATUS_ERR_UNKNOWN)
		return "Something unexpected went wrong";
	if (status == DFU_STATUS_ERR_STALLDPKT)
		return "Device stalled an unexpected request";
	return NULL;
}

/**
 * dfu_target_check_status:
 **/
static gboolean
dfu_target_check_status (DfuTarget *target,
			 GCancellable *cancellable,
			 GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuStatus status;

	/* get the status */
	if (!dfu_device_refresh (priv->device, cancellable, error))
		return FALSE;

	/* not in an error state */
	if (dfu_device_get_state (priv->device) != DFU_STATE_DFU_ERROR)
		return TRUE;

	/* DfuSe-specific long errors */
	status = dfu_device_get_status (priv->device);
	if (dfu_device_has_dfuse_support (priv->device)) {
		if (status == DFU_STATUS_ERR_VENDOR) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "Read protection is active");
			return FALSE;
		}
		if (status == DFU_STATUS_ERR_TARGET) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "Address is wrong or unsupported");
			return FALSE;
		}
	}

	/* use a proper error description */
	g_set_error_literal (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     dfu_target_status_to_error_msg (status));
	return FALSE;
}

/**
 * dfu_target_use_alt_setting:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
static gboolean
dfu_target_use_alt_setting (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *dev;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (priv->device, error))
		return FALSE;

	/* use the correct setting */
	dev = dfu_device_get_usb_dev (priv->device);
	if (dfu_device_get_mode (priv->device) == DFU_MODE_DFU) {
		if (!g_usb_device_set_interface_alt (dev,
						     (gint) dfu_device_get_interface (priv->device),
						     (gint) priv->alt_setting,
						     &error_local)) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "cannot set alternate setting 0x%02x on interface %i: %s",
				     priv->alt_setting,
				     dfu_device_get_interface (priv->device),
				     error_local->message);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * dfu_target_setup:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
static gboolean
dfu_target_setup (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->done_setup)
		return TRUE;

	/* get string */
	if (priv->alt_idx != 0x00) {
		GUsbDevice *dev;
		dev = dfu_device_get_usb_dev (priv->device);
		priv->alt_name =
			g_usb_device_get_string_descriptor (dev,
							    priv->alt_idx,
							    NULL);
	}

	/* parse the DfuSe format according to UM0424 */
	if (!dfu_target_parse_sectors (target,
				       priv->alt_name,
				       error))
		return FALSE;

	/* add a dummy entry */
	if (priv->sectors->len == 0) {
		DfuSector *sector;
		sector = dfu_sector_new (0x0, /* addr */
					 0x0, /* size */
					 0x0, /* size_left */
					 0x0, /* zone */
					 0x0, /* number */
					 DFU_SECTOR_CAP_READABLE |
					 DFU_SECTOR_CAP_WRITEABLE);
		g_debug ("no UM0424 sector descripton in %s", priv->alt_name);
		g_ptr_array_add (priv->sectors, sector);
	}

	priv->done_setup = TRUE;
	return TRUE;
}

/**
 * dfu_target_download_chunk:
 **/
static gboolean
dfu_target_download_chunk (DfuTarget *target, guint8 index, GBytes *bytes,
			   GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;
	gsize actual_length;

	if (!g_usb_device_control_transfer (dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_DNLOAD,
					    index,
					    dfu_device_get_interface (priv->device),
					    (guint8 *) g_bytes_get_data (bytes, NULL),
					    g_bytes_get_size (bytes),
					    &actual_length,
					    dfu_device_get_timeout (priv->device),
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_device_error_fixup (priv->device, cancellable, &error_local);
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "cannot download data: %s",
			     error_local->message);
		return FALSE;
	}

	/* for ST devices, the action only occurs when we do GetStatus */
	if (!dfu_target_check_status (target, cancellable, error))
		return FALSE;

	g_assert (actual_length == g_bytes_get_size (bytes));
	return TRUE;
}

/**
 * dfu_target_set_address:
 * @target: a #DfuTarget
 * @address: memory address
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Sets the address used for the next download or upload request.
 *
 * IMPORTANT: This only works on DfuSe-capable devices from ST.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
static gboolean
dfu_target_set_address (DfuTarget *target,
			guint32 address,
			GCancellable *cancellable,
			GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GBytes *data_in;
	guint8 buf[5];

	/* invalid */
	if (!dfu_device_has_dfuse_support (priv->device)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "only supported for DfuSe targets");
		return FALSE;
	}

	/* format buffer */
	buf[0] = DFU_CMD_DFUSE_SET_ADDRESS_POINTER;
	memcpy (buf + 1, &address, 4);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error))
		return FALSE;

	/* for ST devices, the action only occurs when we do GetStatus */
	if (!dfu_target_check_status (target, cancellable, error))
		return FALSE;
	return TRUE;
}

/**
 * dfu_target_erase_address:
 * @target: a #DfuTarget
 * @address: memory address
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Erases a memory sector at a given address.
 *
 * IMPORTANT: This only works on DfuSe-capable devices from ST.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
static gboolean
dfu_target_erase_address (DfuTarget *target,
			  guint32 address,
			  GCancellable *cancellable,
			  GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GBytes *data_in;
	guint8 buf[5];

	/* invalid */
	if (!dfu_device_has_dfuse_support (priv->device)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "only supported for DfuSe targets");
		return FALSE;
	}

	/* format buffer */
	buf[0] = DFU_CMD_DFUSE_ERASE;
	memcpy (buf + 1, &address, 4);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error))
		return FALSE;

	/* for ST devices, the action only occurs when we do GetStatus */
	if (!dfu_target_check_status (target, cancellable, error))
		return FALSE;

	/* 2nd check required to get error code */
	return dfu_target_check_status (target, cancellable, error);
}

#if 0

/**
 * dfu_target_mass_erase:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Mass erases the device clearing all SRAM and EEPROM memory.
 *
 * This may not be supported on all devices, a better way of doing this action
 * is to enable read protection and then doing dfu_target_read_unprotect().
 *
 * IMPORTANT: This only works on DfuSe-capable devices from ST.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
static gboolean
dfu_target_mass_erase (DfuTarget *target,
		       GCancellable *cancellable,
		       GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GBytes *data_in;
	guint8 buf[1];

	/* invalid */
	if (!dfu_device_has_dfuse_support (priv->device)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "only supported for DfuSe targets");
		return FALSE;
	}

	/* format buffer */
	buf[0] = DFU_CMD_DFUSE_ERASE;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error))
		return FALSE;

	/* for ST devices, the action only occurs when we do GetStatus */
	if (!dfu_target_check_status (target, cancellable, error))
		return FALSE;

	/* 2nd check required to get error code */
	return dfu_target_check_status (target, cancellable, error);
}

/**
 * dfu_target_read_unprotect:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Turns of read protection on the device, clearing all SRAM and EEPROM memory.
 *
 * IMPORTANT: This only works on DfuSe-capable devices from ST.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
static gboolean
dfu_target_read_unprotect (DfuTarget *target,
			   GCancellable *cancellable,
			   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GBytes *data_in;
	guint8 buf[5];

	/* invalid */
	if (!dfu_device_has_dfuse_support (priv->device)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "only supported for DfuSe targets");
		return FALSE;
	}

	/* format buffer */
	buf[0] = DFU_CMD_DFUSE_READ_UNPROTECT;
	memcpy (buf + 1, &address, 4);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error))
		return FALSE;

	/* for ST devices, the action only occurs when we do GetStatus */
	return dfu_target_check_status (target, cancellable, error);
}

#endif

/**
 * dfu_target_upload_chunk: (skip)
 **/
GBytes *
dfu_target_upload_chunk (DfuTarget *target, guint8 index,
			 GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;
	guint8 *buf;
	gsize actual_length;
	guint16 transfer_size = dfu_device_get_transfer_size (priv->device);

	buf = g_new0 (guint8, transfer_size);
	if (!g_usb_device_control_transfer (dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_UPLOAD,
					    index,
					    dfu_device_get_interface (priv->device),
					    buf, (gsize) transfer_size,
					    &actual_length,
					    dfu_device_get_timeout (priv->device),
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_device_error_fixup (priv->device, cancellable, &error_local);
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "cannot upload data: %s",
			     error_local->message);
		return NULL;
	}

	/* for ST devices, the action only occurs when we do GetStatus */
	if (!dfu_device_has_quirk (priv->device, DFU_DEVICE_QUIRK_NO_GET_STATUS_UPLOAD)) {
		if (!dfu_target_check_status (target, cancellable, error))
			return NULL;
	}

	return g_bytes_new_take (buf, actual_length);
}

/**
 * dfu_target_upload_element:
 **/
static DfuElement *
dfu_target_upload_element (DfuTarget *target,
			   guint32 address,
			   gsize expected_size,
			   GCancellable *cancellable,
			   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	DfuElement *element = NULL;
	GBytes *chunk_tmp;
	gsize chunk_size;
	gsize offset = 0;
	gsize total_size = 0;
	guint16 transfer_size = dfu_device_get_transfer_size (priv->device);
	guint8 *buffer;
	guint32 last_sector_id = G_MAXUINT;
	guint dfuse_sector_offset = 0;
	guint i;
	guint old_percentage = G_MAXUINT;
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* ST uses wBlockNum=0 for DfuSe commands and wBlockNum=1 is reserved */
	if (dfu_device_has_dfuse_support (priv->device)) {
		offset += address;
		dfuse_sector_offset = 2;
	}

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (i = 0; i < 0xffff; i++) {

		/* for DfuSe devices we need to handle the address manually */
		if (dfu_device_has_dfuse_support (priv->device)) {

			/* check the sector with this element address is suitable */
			sector = dfu_target_get_sector_for_addr (target, offset);
			if (sector == NULL) {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_DEVICE,
					     "no memory sector at 0x%04x",
					     (guint) offset);
				return NULL;
			}
			if (!dfu_sector_has_cap (sector, DFU_SECTOR_CAP_READABLE)) {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_DEVICE,
					     "memory sector at 0x%04x is not readble",
					     (guint) offset);
				return NULL;
			}

			/* manually set the sector address */
			if (dfu_sector_get_id (sector) != last_sector_id) {
				g_debug ("setting DfuSe address to 0x%04x", (guint) offset);
				if (!dfu_target_set_address (target,
							     offset,
							     cancellable,
							     error))
					return NULL;
				last_sector_id = dfu_sector_get_id (sector);
			}
		}

		/* read chunk of data */
		chunk_tmp = dfu_target_upload_chunk (target,
						     i + dfuse_sector_offset,
						     cancellable,
						     error);
		if (chunk_tmp == NULL)
			return NULL;

		/* keep a sum of all the chunks */
		chunk_size = g_bytes_get_size (chunk_tmp);
		total_size += chunk_size;
		offset += chunk_size;

		/* add to array */
		g_debug ("got #%04x chunk of size %" G_GSIZE_FORMAT,
			 i, chunk_size);
		g_ptr_array_add (chunks, chunk_tmp);

		/* update UI */
		if (chunk_size > 0) {
			guint percentage = (total_size * 100) / expected_size;
			if (percentage != old_percentage) {
				g_signal_emit (target,
					       signals[SIGNAL_PERCENTAGE_CHANGED],
					       0, percentage);
			}
		}

		/* detect short write as EOF */
		if (chunk_size < transfer_size)
			break;
	}

	/* check final size */
	if (expected_size > 0) {
		if (total_size != expected_size) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "invalid size, got %" G_GSIZE_FORMAT ", "
				     "expected %" G_GSIZE_FORMAT ,
				     total_size, expected_size);
			return NULL;
		}
	}

	/* stitch them all together */
	offset = 0;
	buffer = g_malloc0 (total_size);
	for (i = 0; i < chunks->len; i++) {
		const guint8 *chunk_data;
		chunk_tmp = g_ptr_array_index (chunks, i);
		chunk_data = g_bytes_get_data (chunk_tmp, &chunk_size);
		memcpy (buffer + offset, chunk_data, chunk_size);
		offset += chunk_size;
	}

	/* create new image */
	contents = g_bytes_new_take (buffer, total_size);
	element = dfu_element_new ();
	dfu_element_set_contents (element, contents);
	return element;
}

/**
 * dfu_target_upload:
 * @target: a #DfuTarget
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Uploads firmware from the target to the host.
 *
 * Return value: (transfer full): the uploaded image, or %NULL for error
 *
 * Since: 0.5.4
 **/
DfuImage *
dfu_target_upload (DfuTarget *target,
		   DfuTargetTransferFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	guint i;
	guint32 last_sector_id = G_MAXUINT;
	g_autoptr(DfuImage) image = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure populated */
	if (!dfu_target_setup (target, error))
		return NULL;

	/* can the target do this? */
	if (!dfu_device_can_upload (priv->device)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "target cannot do uploading");
		return NULL;
	}

	/* use correct alt */
	if (!dfu_target_use_alt_setting (target, error))
		return NULL;

	/* no open?! */
	if (priv->sectors->len == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "no sectors defined for target");
		return NULL;
	}

	/* create a new image */
	image = dfu_image_new ();
	dfu_image_set_name (image, priv->alt_name);
	dfu_image_set_alt_setting (image, priv->alt_setting);

	/* get all the sectors for the device */
	for (i = 0; i < priv->sectors->len; i++) {
		g_autoptr(DfuElement) element = NULL;

		/* only upload to the start of any zone:sector */
		sector = g_ptr_array_index (priv->sectors, i);
		if (dfu_sector_get_id (sector) == last_sector_id)
			continue;

		/* get the first element from the hardware */
		g_debug ("starting upload from 0x%08x (0x%04x)",
			 dfu_sector_get_address (sector),
			 dfu_sector_get_size_left (sector));
		element = dfu_target_upload_element (target,
						     dfu_sector_get_address (sector),
						     dfu_sector_get_size_left (sector),
						     cancellable,
						     error);
		if (element == NULL)
			return NULL;

		/* this element was uploaded okay */
		dfu_image_add_element (image, element);

		/* ignore sectors until one of these changes */
		last_sector_id = dfu_sector_get_id (sector);
	}

	/* do host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_ATTACH) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) > 0) {
		if (!dfu_device_attach (priv->device, error))
			return NULL;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) {
		g_debug ("booting to runtime");
		if (!dfu_device_wait_for_replug (priv->device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 cancellable,
						 error))
			return NULL;
	}

	/* success */
	return g_object_ref (image);
}

/**
 * _g_bytes_compare_verbose:
 **/
static gchar *
_g_bytes_compare_verbose (GBytes *bytes1, GBytes *bytes2)
{
	const guint8 *data1;
	const guint8 *data2;
	gsize length1;
	gsize length2;
	guint i;

	data1 = g_bytes_get_data (bytes1, &length1);
	data2 = g_bytes_get_data (bytes2, &length2);

	/* not the same length */
	if (length1 != length2) {
		return g_strdup_printf ("got %" G_GSIZE_FORMAT " bytes, "
					"expected %" G_GSIZE_FORMAT,
					length1, length2);
	}

	/* return 00 01 02 03 */
	for (i = 0; i < length1; i++) {
		if (data1[i] != data2[i]) {
			return g_strdup_printf ("got 0x%02x, expected 0x%02x @ 0x%04x",
						data1[i], data2[i], i);
		}
	}
	return NULL;
}

/**
 * dfu_target_download_element:
 **/
static gboolean
dfu_target_download_element (DfuTarget *target,
			     DfuElement *element,
			     DfuTargetTransferFlags flags,
			     GCancellable *cancellable,
			     GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	GBytes *bytes;
	guint i;
	guint nr_chunks;
	guint dfuse_sector_offset = 0;
	guint last_sector_id = G_MAXUINT;
	guint old_percentage = G_MAXUINT;
	guint16 transfer_size = dfu_device_get_transfer_size (priv->device);
	g_autoptr(GError) error_local = NULL;

	/* ST uses wBlockNum=0 for DfuSe commands and wBlockNum=1 is reserved */
	if (dfu_device_has_dfuse_support (priv->device))
		dfuse_sector_offset = 2;

	/* round up as we have to transfer incomplete blocks */
	bytes = dfu_element_get_contents (element);
	nr_chunks = ceil ((gdouble) g_bytes_get_size (bytes) /
			  (gdouble) transfer_size);
	if (nr_chunks == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "zero-length firmware");
		return FALSE;
	}
	for (i = 0; i < nr_chunks + 1; i++) {
		gsize length;
		gsize offset;
		guint percentage;
		g_autoptr(GBytes) bytes_tmp = NULL;

		/* caclulate the offset into the element data */
		offset = i * transfer_size;

		/* for DfuSe devices we need to handle the erase and setting
		 * the address manually */
		if (dfu_device_has_dfuse_support (priv->device)) {

			/* check the sector with this element address is suitable */
			sector = dfu_target_get_sector_for_addr (target, offset);
			if (sector == NULL) {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_DEVICE,
					     "no memory sector at 0x%04x",
					     (guint) offset);
				return FALSE;
			}
			if (!dfu_sector_has_cap (sector, DFU_SECTOR_CAP_WRITEABLE)) {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_DEVICE,
					     "memory sector at 0x%04x is not writable",
					     (guint) offset);
				return FALSE;
			}

			/* if it's erasable and not yet blanked */
			if (!dfu_sector_has_cap (sector, DFU_SECTOR_CAP_ERASEABLE) &&
			    g_hash_table_lookup (priv->sectors_erased, sector) == NULL) {
				g_debug ("erasing DfuSe address at 0x%04x", (guint) offset);
				if (!dfu_target_erase_address (target,
							       offset,
							       cancellable,
							       error))
					return FALSE;
				g_hash_table_insert (priv->sectors_erased,
						     sector,
						     GINT_TO_POINTER (1));
			}

			/* manually set the sector address */
			if (dfu_sector_get_id (sector) != last_sector_id) {
				g_debug ("setting DfuSe address to 0x%04x", (guint) offset);
				if (!dfu_target_set_address (target,
							     offset,
							     cancellable,
							     error))
					return FALSE;
				last_sector_id = dfu_sector_get_id (sector);
			}
		}

		/* we have to write one final zero-sized chunk for EOF */
		if (i < nr_chunks) {
			length = g_bytes_get_size (bytes) - offset;
			if (length > transfer_size)
				length = transfer_size;
			bytes_tmp = g_bytes_new_from_bytes (bytes, offset, length);
		} else {
			bytes_tmp = g_bytes_new (NULL, 0);
		}
		g_debug ("writing #%04x chunk of size %" G_GSIZE_FORMAT,
			 i, g_bytes_get_size (bytes_tmp));
		if (!dfu_target_download_chunk (target,
						i + dfuse_sector_offset,
						bytes_tmp,
						cancellable,
						error))
			return FALSE;

		/* update UI */
		percentage = (offset * 100) / g_bytes_get_size (bytes);
		if (percentage != old_percentage) {
			g_signal_emit (target,
				       signals[SIGNAL_PERCENTAGE_CHANGED],
				       0, percentage);
		}

		/* give the target a chance to update */
		g_usleep (dfu_device_get_download_timeout (priv->device) * 1000);

		/* getting the status moves the state machine to DNLOAD-IDLE */
		if (!dfu_device_refresh (priv->device, cancellable, error))
			return FALSE;
	}

	/* verify */
	if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY) {
		GBytes *bytes_tmp;
		g_autoptr(DfuElement) element_tmp = NULL;
		element_tmp = dfu_target_upload_element (target,
							 dfu_element_get_address (element),
							 g_bytes_get_size (bytes),
							 cancellable,
							 error);
		if (element_tmp == NULL)
			return FALSE;
		bytes_tmp = dfu_element_get_contents (element_tmp);
		if (g_bytes_compare (bytes_tmp, bytes) != 0) {
			g_autofree gchar *bytes_cmp_str = NULL;
			bytes_cmp_str = _g_bytes_compare_verbose (bytes_tmp, bytes);
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_VERIFY_FAILED,
				     "verify failed: %s",
				     bytes_cmp_str);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * dfu_target_download:
 * @target: a #DfuTarget
 * @image: a #DfuImage
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Downloads firmware from the host to the target, optionally verifying
 * the transfer.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_download (DfuTarget *target, DfuImage *image,
		     DfuTargetTransferFlags flags,
		     GCancellable *cancellable,
		     GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuElement *element;
	GPtrArray *elements;
	gboolean ret;
	guint i;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (DFU_IS_IMAGE (image), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure populated */
	if (!dfu_target_setup (target, error))
		return FALSE;

	/* can the target do this? */
	if (!dfu_device_can_download (priv->device)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "target cannot do downloading");
		return FALSE;
	}

	/* use correct alt */
	if (!dfu_target_use_alt_setting (target, error))
		return FALSE;

	/* mark these as all erased */
	if (dfu_device_has_dfuse_support (priv->device))
		g_hash_table_remove_all (priv->sectors_erased);

	/* download all elements in the image to the device */
	elements = dfu_image_get_elements (image);
	if (elements->len == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "no image elements");
		return FALSE;
	}
	for (i = 0; i < elements->len; i++) {
		element = dfu_image_get_element (image, i);
		g_debug ("downloading element at 0x%04x",
			 dfu_element_get_address (element));
		ret = dfu_target_download_element (target,
						   element,
						   flags,
						   cancellable,
						   error);
		if (!ret)
			return FALSE;
	}

	/* attempt to switch back to runtime */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_ATTACH) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) > 0) {
		if (!dfu_device_attach (priv->device, error))
			return FALSE;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_WAIT_RUNTIME) {
		g_debug ("booting to runtime to set auto-boot");
		if (!dfu_device_wait_for_replug (priv->device,
						 DFU_DEVICE_REPLUG_TIMEOUT,
						 cancellable,
						 error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

#if 0
/**
 * dfu_target_get_commands:
 **/
static gboolean
dfu_target_get_commands (DfuTarget *target,
			 GCancellable *cancellable,
			 GError **error)
{
	GBytes *data_in;
	GBytes *data_out;
	guint8 buf[1];

	/* invalid */
	if (!dfu_device_has_dfuse_support (priv->device)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "only supported for DfuSe targets");
		return FALSE;
	}

	/* format buffer */
	buf[0] = DFU_CMD_DFUSE_GET_COMMAND;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error))
		return FALSE;

	/* return results */
	data_out = dfu_target_upload_chunk (target, 0, cancellable, error);
	if (data_out == NULL)
		return FALSE;

	// N bytes,
	// each byte is the command code

	// FIXME: parse?
	return TRUE;
}
#endif

/**
 * dfu_target_get_alt_setting:
 * @target: a #DfuTarget
 *
 * Gets the alternate setting to use for this interface.
 *
 * Return value: the alternative setting, typically zero
 *
 * Since: 0.5.4
 **/
guint8
dfu_target_get_alt_setting (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0xff);
	return priv->alt_setting;
}

/**
 * dfu_target_get_alt_name:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Gets the alternate setting name to use for this interface.
 *
 * Return value: the alternative setting name, typically %NULL
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_target_get_alt_name (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);

	/* ensure populated */
	if (!dfu_target_setup (target, error))
		return NULL;

	/* nothing */
	if (priv->alt_name == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_FOUND,
				     "no alt-name");
		return NULL;
	}

	return priv->alt_name;
}

/**
 * dfu_target_get_cipher_kind:
 * @target: a #DfuTarget
 *
 * Gets the cipher used for data sent to this interface.
 *
 * Return value: the cipher, typically %DFU_CIPHER_KIND_NONE
 *
 * Since: 0.5.4
 **/
DfuCipherKind
dfu_target_get_cipher_kind (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0);
	return priv->cipher_kind;
}
