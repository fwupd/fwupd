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
	DFU_ATTRIBUTE_NONE			= 0,
	DFU_ATTRIBUTE_CAN_DOWNLOAD		= (1 << 0),
	DFU_ATTRIBUTE_CAN_UPLOAD		= (1 << 1),
	DFU_ATTRIBUTE_MANIFEST_TOL		= (1 << 2),
	DFU_ATTRIBUTE_WILL_DETACH		= (1 << 3),
	DFU_ATTRIBUTE_CAN_ACCELERATE		= (1 << 7),
	DFU_ATTRIBUTE_LAST
} DfuAttributes;

typedef enum {
	DFU_QUIRK_NONE				= 0,
	DFU_QUIRK_IGNORE_POLLTIMEOUT		= (1 << 0),
	DFU_QUIRK_FORCE_DFU_MODE		= (1 << 1),
	DFU_QUIRK_IGNORE_INVALID_VERSION	= (1 << 2),
	DFU_QUIRK_USE_PROTOCOL_ZERO		= (1 << 3),
	DFU_QUIRK_LAST
} DfuQuirks;

typedef enum {
	DFU_CMD_DFUSE_GET_COMMAND		= 0x00,
	DFU_CMD_DFUSE_SET_ADDRESS_POINTER	= 0x21,
	DFU_CMD_DFUSE_ERASE			= 0x41,
	DFU_CMD_DFUSE_READ_UNPROTECT		= 0x92,
} DfuCmdDfuse;

/**
 * DfuTargetPrivate:
 *
 * Private #DfuTarget data
 **/
typedef struct {
	DfuMode			 mode;
	DfuState		 state;
	DfuStatus		 status;
	DfuDevice		*device;
	gboolean		 interface_claimed;
	gboolean		 dfuse_supported;
	guint16			 transfer_size;
	guint8			 iface_number;
	guint8			 iface_alt_setting;
	guint8			 iface_alt_setting_idx;
	gchar			*iface_alt_setting_name;
	guint			 dnload_timeout;
	guint			 timeout_ms;
	DfuAttributes		 attributes;
	DfuQuirks		 quirks;
	GPtrArray		*sectors; /* of DfuSector */
	GHashTable		*sectors_erased; /* of DfuSector:1 */
} DfuTargetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuTarget, dfu_target, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_target_get_instance_private (o))

/**
 * dfu_target_class_init:
 **/
static void
dfu_target_class_init (DfuTargetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_target_finalize;
}

/**
 * dfu_target_init:
 **/
static void
dfu_target_init (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	priv->state = DFU_STATE_APP_IDLE;
	priv->status = DFU_STATUS_OK;
	priv->timeout_ms = 500;
	priv->transfer_size = 64;
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

	g_free (priv->iface_alt_setting_name);
	g_ptr_array_unref (priv->sectors);
	g_hash_table_unref (priv->sectors_erased);
	if (priv->device != NULL)
		g_object_unref (priv->device);

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

	/* optional space */
	if (tmp[0] == ' ')
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
	guint i;
	guint j;
	g_autofree gchar *str_debug = NULL;
	g_auto(GStrv) zones = NULL;

	/* not set */
	if (alt_name == NULL)
		return TRUE;

	/* not a DfuSe alternative name */
	if (alt_name[0] != '@')
		return TRUE;

	/* clear any existing zones */
	g_ptr_array_set_size (priv->sectors, 0);

	/* parse zones */
	zones = g_strsplit (alt_name, "/", -1);
	g_debug ("DfuSe nice alt-name: %s", g_strchomp (zones[0] + 1));
	for (i = 1; zones[i] != NULL; i += 2) {
		guint64 addr;
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

typedef struct __attribute__((packed)) {
	guint8		bLength;
	guint8		bDescriptorType;
	guint8		bmAttributes;
	guint16		wDetachTimeOut;
	guint16		wTransferSize;
	guint16		bcdDFUVersion;
} DfuFuncDescriptor;

/**
 * dfu_target_get_quirks:
 **/
static DfuQuirks
dfu_target_get_quirks (GUsbDevice *dev)
{
	DfuQuirks quirks = DFU_QUIRK_NONE;
	guint16 vid, pid, release;

	vid = g_usb_device_get_vid (dev);
	pid = g_usb_device_get_pid (dev);
	release = g_usb_device_get_release (dev);

	/* Openmoko Freerunner / GTA02 */
	if ((vid == 0x1d50 || vid == 0x1457) &&
	    pid >= 0x5117 && pid <= 0x5126)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* OpenPCD Reader */
	if (vid == 0x16c0 && pid == 0x076b)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* Siemens AG, PXM 40 & PXM 50 */
	if (vid == 0x0908 && (pid == 0x02c4 || pid == 0x02c5) && release == 0x0)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* Midiman M-Audio Transit */
	if (vid == 0x0763 && pid == 0x2806)
		quirks |= DFU_QUIRK_IGNORE_POLLTIMEOUT;

	/* the LPC DFU bootloader uses the wrong mode */
	if (vid == 0x1fc9 && pid == 0x000c)
		quirks |= DFU_QUIRK_FORCE_DFU_MODE;

	/* the Leaflabs Maple3 is known broken */
	if (vid == 0x1eaf && pid == 0x0003 && release == 0x0200)
		quirks |= DFU_QUIRK_IGNORE_INVALID_VERSION;

	/* the DSO Nano has uses 0 instead of 2 when in DFU mode */
//	quirks |= DFU_QUIRK_USE_PROTOCOL_ZERO;

	return quirks;
}

/**
 * dfu_target_update_from_iface:
 **/
static gboolean
dfu_target_update_from_iface (DfuTarget *target, GUsbInterface *iface)
{
	DfuMode mode = DFU_MODE_UNKNOWN;
	DfuQuirks quirks;
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GBytes *iface_data = NULL;
	GUsbDevice *dev;
	const DfuFuncDescriptor *desc;
	gsize iface_data_length;

	/* runtime */
	if (g_usb_interface_get_protocol (iface) == 0x01)
		mode = DFU_MODE_RUNTIME;

	/* DFU */
	if (g_usb_interface_get_protocol (iface) == 0x02)
		mode = DFU_MODE_DFU;

	/* the DSO Nano has uses 0 instead of 2 when in DFU mode */
	dev = _dfu_device_get_usb_dev (priv->device);
	quirks = dfu_target_get_quirks (dev);
	if ((quirks & DFU_QUIRK_USE_PROTOCOL_ZERO) &&
	    g_usb_interface_get_protocol (iface) == 0x00)
		mode = DFU_MODE_DFU;

	/* nothing found */
	if (mode == DFU_MODE_UNKNOWN)
		return FALSE;

	/* in DFU mode, the interface is supposed to be 0 */
	if (mode == DFU_MODE_DFU && g_usb_interface_get_number (iface) != 0)
		g_warning ("iface has to be 0 in DFU mode, got 0x%02i",
			   g_usb_interface_get_number (iface));

	/* some devices set the wrong mode */
	if (quirks & DFU_QUIRK_FORCE_DFU_MODE)
		mode = DFU_MODE_DFU;

	/* save for reset */
	if (mode == DFU_MODE_RUNTIME) {
		_dfu_device_set_runtime_vid (priv->device, g_usb_device_get_vid (dev));
		_dfu_device_set_runtime_pid (priv->device, g_usb_device_get_pid (dev));
	}

	/* update */
	priv->iface_number = g_usb_interface_get_number (iface);
	priv->iface_alt_setting = g_usb_interface_get_alternate (iface);
	priv->iface_alt_setting_idx = g_usb_interface_get_index (iface);
	priv->quirks = quirks;
	priv->mode = mode;

	/* parse the functional descriptor */
	iface_data = g_usb_interface_get_extra (iface);
	desc = g_bytes_get_data (iface_data, &iface_data_length);
	if (iface_data_length != 0x09) {
		g_warning ("interface found, but no interface data");
		return FALSE;
	}

	/* check sanity */
	if (desc->bLength != 0x09) {
		g_warning ("DFU interface data has incorrect length: 0x%02x",
			   desc->bLength);
	}

	/* check transfer size */
	priv->transfer_size = desc->wTransferSize;
	if (priv->transfer_size == 0x0000) {
		g_warning ("DFU transfer size invalid, using default: 0x%04x",
			   desc->wTransferSize);
		priv->transfer_size = 64;
	}

	/* check DFU version */
	if (quirks & DFU_QUIRK_IGNORE_INVALID_VERSION) {
		g_debug ("ignoring quirked DFU version");
	} else {
		if (desc->bcdDFUVersion == 0x0100 ||
		    desc->bcdDFUVersion == 0x0101) {
			g_debug ("basic DFU, no DfuSe support");
			priv->dfuse_supported = FALSE;
		} else if (desc->bcdDFUVersion == 0x011a) {
			g_debug ("DfuSe support");
			priv->dfuse_supported = TRUE;
		} else {
			g_warning ("DFU version is invalid: 0x%04x",
				   desc->bcdDFUVersion);
		}
	}

	/* ST-specific */
	if (priv->dfuse_supported &&
	    desc->bmAttributes & DFU_ATTRIBUTE_CAN_ACCELERATE)
		priv->transfer_size = 0x1000;

	/* get attributes about the DFU operation */
	priv->attributes = desc->bmAttributes;
	return TRUE;
}

/**
 * _dfu_target_new:
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
_dfu_target_new (DfuDevice *device, GUsbInterface *iface)
{
	DfuTargetPrivate *priv;
	DfuTarget *target;
	target = g_object_new (DFU_TYPE_TARGET, NULL);
	priv = GET_PRIVATE (target);
	priv->device = g_object_ref (device);
	if (!dfu_target_update_from_iface (target, iface)) {
		g_object_unref (target);
		return NULL;
	}
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
 * dfu_target_get_mode:
 * @target: a #GUsbDevice
 *
 * Gets the target mode.
 *
 * Return value: enumerated mode, e.g. %DFU_MODE_RUNTIME
 *
 * Since: 0.5.4
 **/
DfuMode
dfu_target_get_mode (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0);
	return priv->mode;
}

/**
 * dfu_target_get_state:
 * @target: a #GUsbDevice
 *
 * Gets the target state.
 *
 * Return value: enumerated state, e.g. %DFU_STATE_DFU_UPLOAD_IDLE
 *
 * Since: 0.5.4
 **/
DfuState
dfu_target_get_state (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0);
	return priv->state;
}

/**
 * dfu_target_get_status:
 * @target: a #GUsbDevice
 *
 * Gets the target status.
 *
 * Return value: enumerated status, e.g. %DFU_STATUS_ERR_ADDRESS
 *
 * Since: 0.5.4
 **/
DfuStatus
dfu_target_get_status (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0);
	return priv->status;
}

/**
 * dfu_target_can_upload:
 * @target: a #GUsbDevice
 *
 * Gets if the target can upload.
 *
 * Return value: %TRUE if the target can upload from target to host
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_can_upload (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	return (priv->attributes & DFU_ATTRIBUTE_CAN_UPLOAD) > 0;
}

/**
 * dfu_target_can_download:
 * @target: a #GUsbDevice
 *
 * Gets if the target can download.
 *
 * Return value: %TRUE if the target can download from host to target
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_can_download (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	return (priv->attributes & DFU_ATTRIBUTE_CAN_DOWNLOAD) > 0;
}

/**
 * dfu_target_get_transfer_size:
 * @target: a #GUsbDevice
 *
 * Gets the transfer size in bytes.
 *
 * Return value: packet size, or 0 for unknown
 *
 * Since: 0.5.4
 **/
guint16
dfu_target_get_transfer_size (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0xffff);
	return priv->transfer_size;
}

/**
 * dfu_target_set_transfer_size:
 * @target: a #GUsbDevice
 * @transfer_size: maximum packet size
 *
 * Sets the transfer size in bytes.
 *
 * Since: 0.5.4
 **/
void
dfu_target_set_transfer_size (DfuTarget *target, guint16 transfer_size)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_if_fail (DFU_IS_TARGET (target));
	priv->transfer_size = transfer_size;
}

/**
 * dfu_target_error_fixup:
 **/
static void
dfu_target_error_fixup (DfuTarget *target,
			GCancellable *cancellable,
			GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);

	/* sad panda */
	if (error == NULL)
		return;

	/* not the right error to query */
	if (!g_error_matches (*error,
			      G_USB_DEVICE_ERROR,
			      G_USB_DEVICE_ERROR_NOT_SUPPORTED))
		return;

	/* get the status */
	if (!dfu_target_refresh (target, cancellable, NULL))
		return;

	/* not in an error state */
	if (priv->state != DFU_STATE_DFU_ERROR)
		return;

	/* prefix the error */
	switch (priv->status) {
	case DFU_STATUS_OK:
		/* ignore */
		break;
	case DFU_STATUS_ERR_VENDOR:
		g_prefix_error (error, "read protection is active: ");
		break;
	default:
		g_prefix_error (error, "[%s,%s]: ",
				dfu_state_to_string (priv->state),
				dfu_status_to_string (priv->status));
		break;
	}
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

	/* get the status */
	if (!dfu_target_refresh (target, cancellable, error))
		return FALSE;

	/* not in an error state */
	if (priv->state != DFU_STATE_DFU_ERROR)
		return TRUE;

	/* read protection */
	if (priv->dfuse_supported) {
		if (priv->status == DFU_STATUS_ERR_VENDOR) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "failed, read protection is active");
			return FALSE;
		}
		if (priv->status == DFU_STATUS_ERR_TARGET) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "failed, address is wrong or unsupported");
			return FALSE;
		}
	}

	/* prefix the error */
	g_set_error (error,
		     DFU_ERROR,
		     DFU_ERROR_NOT_SUPPORTED,
		     "failed, state:%s status:%s]: ",
		     dfu_state_to_string (priv->state),
		     dfu_status_to_string (priv->status));
	return FALSE;
}

/**
 * dfu_target_open:
 * @target: a #DfuTarget
 * @flags: #DfuTargetOpenFlags, e.g. %DFU_TARGET_OPEN_FLAG_NONE
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_open (DfuTarget *target, DfuTargetOpenFlags flags,
		 GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *dev;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->interface_claimed)
		return TRUE;

	/* ensure parent device is open */
	if (!dfu_device_open (priv->device, error))
		return FALSE;

	/* claim the correct interface */
	dev = _dfu_device_get_usb_dev (priv->device);
	if (!g_usb_device_claim_interface (dev, (gint) priv->iface_number, 0, &error_local)) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INVALID_DEVICE,
			     "cannot claim interface %i: %s",
			     priv->iface_number, error_local->message);
		return FALSE;
	}
	priv->interface_claimed = TRUE;

	/* use the correct setting */
	if (priv->mode == DFU_MODE_DFU) {
		if (!g_usb_device_set_interface_alt (dev,
						     (gint) priv->iface_number,
						     (gint) priv->iface_alt_setting,
						     &error_local)) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "cannot set alternate setting 0x%02x on interface %i: %s",
				     priv->iface_alt_setting,
				     priv->iface_number,
				     error_local->message);
			return FALSE;
		}
	}

	/* get string */
	if (priv->iface_alt_setting_idx != 0x00) {
		priv->iface_alt_setting_name =
			g_usb_device_get_string_descriptor (dev,
							    priv->iface_alt_setting_idx,
							    NULL);
	}

	/* automatically abort any uploads or downloads */
	if ((flags & DFU_TARGET_OPEN_FLAG_NO_AUTO_REFRESH) == 0) {
		if (!dfu_target_refresh (target, cancellable, error))
			return FALSE;
		switch (priv->state) {
		case DFU_STATE_DFU_UPLOAD_IDLE:
		case DFU_STATE_DFU_DNLOAD_IDLE:
		case DFU_STATE_DFU_DNLOAD_SYNC:
			g_debug ("aborting transfer %s", dfu_status_to_string (priv->status));
			if (!dfu_target_abort (target, cancellable, error))
				return FALSE;
			break;
		case DFU_STATE_DFU_ERROR:
			g_debug ("clearing error %s", dfu_status_to_string (priv->status));
			if (!dfu_target_clear_status (target, cancellable, error))
				return FALSE;
			break;
		default:
			break;
		}
	}

	/* parse the DfuSe format according to UM0424 */
	if (!dfu_target_parse_sectors (target,
				       priv->iface_alt_setting_name,
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
		g_debug ("no UM0424 sector descripton, so adding dummy");
		g_ptr_array_add (priv->sectors, sector);
	}

	return TRUE;
}

/**
 * dfu_target_close:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Closes a DFU-capable target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_close (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *dev;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* this is our intention; the release might fail if the USB device
	 * has been disconnected already */
	priv->interface_claimed = FALSE;

	/* only release if claimed */
	if (priv->interface_claimed) {
		dev = _dfu_device_get_usb_dev (priv->device);
		if (!g_usb_device_release_interface (dev,
						     (gint) priv->iface_number,
						     0,
						     &error_local)) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "cannot release interface %i: %s",
				     priv->iface_number, error_local->message);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

/**
 * dfu_target_refresh:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Refreshes the cached properties on the DFU target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_refresh (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	gsize actual_length = 0;
	guint8 buf[6];
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_GETSTATUS,
					    0,
					    priv->iface_number,
					    buf, sizeof(buf), &actual_length,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "cannot get target state: %s",
			     error_local->message);
		return FALSE;
	}
	if (actual_length != 6) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "cannot get target status, invalid size: %04x",
			     (guint) actual_length);
	}
	priv->status = buf[0];
	if (priv->quirks & DFU_QUIRK_IGNORE_POLLTIMEOUT) {
		priv->dnload_timeout = 5;
	} else {
		priv->dnload_timeout = buf[1] +
					(((guint32) buf[2]) << 8) +
					(((guint32) buf[3]) << 16);
	}
	priv->state = buf[4];
	g_debug ("refreshed status=%s and state=%s",
		 dfu_status_to_string (priv->status),
		 dfu_state_to_string (priv->state));
	return TRUE;
}

/**
 * dfu_target_detach:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Detaches the target putting it into DFU-mode.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_detach (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_DETACH,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_target_error_fixup (target, cancellable, &error_local);
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "cannot detach target: %s",
			     error_local->message);
		return FALSE;
	}

	/* do a host reset */
	if ((priv->attributes & DFU_ATTRIBUTE_WILL_DETACH) == 0) {
		g_debug ("doing target reset as host will not self-reset");
		if (!dfu_device_reset (priv->device, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_abort:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Aborts any upload or download in progress.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_abort (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_ABORT,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_target_error_fixup (target, cancellable, &error_local);
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "cannot abort target: %s",
			     error_local->message);
		return FALSE;
	}

	return TRUE;
}

/**
 * _dfu_target_update:
 * @target: a #DfuTarget
 * @iface: a #GUsbInterface
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Updates the target with new interface data. This only needs to be
 * done after the device has been reset.
 *
 * Returns: %TRUE for success
 **/
gboolean
_dfu_target_update (DfuTarget *target, GUsbInterface *iface,
		    GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	gboolean reclaim_interface = FALSE;

	/* close */
	if (priv->interface_claimed) {
		if (!dfu_target_close (target, error))
			return FALSE;
		reclaim_interface = TRUE;
	}

	/* check this is _still_ a DFU-capable target */
	if (!dfu_target_update_from_iface (target, iface)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "replugged target is not DFU-capable");
		return FALSE;
	}

	/* reclaim */
	if (reclaim_interface) {
		if (!dfu_device_open (priv->device, error))
			return FALSE;
		if (!dfu_target_open (target, DFU_TARGET_OPEN_FLAG_NONE,
				      cancellable, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_clear_status:
 * @target: a #DfuTarget
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Clears any error status on the DFU target.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_target_clear_status (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_CLRSTATUS,
					    0,
					    priv->iface_number,
					    NULL, 0, NULL,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_target_error_fixup (target, cancellable, &error_local);
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "cannot clear status on the target: %s",
			     error_local->message);
		return FALSE;
	}
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

	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_DNLOAD,
					    index,
					    priv->iface_number,
					    (guint8 *) g_bytes_get_data (bytes, NULL),
					    g_bytes_get_size (bytes),
					    &actual_length,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_target_error_fixup (target, cancellable, &error_local);
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
	if (!priv->dfuse_supported) {
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
	if (!priv->dfuse_supported) {
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
	if (!priv->dfuse_supported) {
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
	if (!priv->dfuse_supported) {
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
 * dfu_target_upload_chunk:
 **/
static GBytes *
dfu_target_upload_chunk (DfuTarget *target, guint8 index,
			 GCancellable *cancellable, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autoptr(GError) error_local = NULL;
	guint8 *buf;
	gsize actual_length;

	buf = g_new0 (guint8, priv->transfer_size);
	if (!g_usb_device_control_transfer (_dfu_device_get_usb_dev (priv->device),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_UPLOAD,
					    index,
					    priv->iface_number,
					    buf, (gsize) priv->transfer_size,
					    &actual_length,
					    priv->timeout_ms,
					    cancellable,
					    &error_local)) {
		/* refresh the error code */
		dfu_target_error_fixup (target, cancellable, &error_local);
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "cannot upload data: %s",
			     error_local->message);
		return NULL;
	}

	/* for ST devices, the action only occurs when we do GetStatus */
	if (!dfu_target_check_status (target, cancellable, error))
		return FALSE;

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
			   DfuProgressCallback progress_cb,
			   gpointer progress_cb_data,
			   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	DfuElement *element = NULL;
	GBytes *chunk_tmp;
	gsize chunk_size;
	gsize offset = 0;
	gsize total_size = 0;
	guint8 *buffer;
	guint32 last_sector_id = G_MAXUINT;
	guint dfuse_sector_offset = 0;
	guint i;
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* ST uses wBlockNum=0 for DfuSe commands and wBlockNum=1 is reserved */
	if (priv->dfuse_supported) {
		offset += address;
		dfuse_sector_offset = 2;
	}

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (i = 0; i < 0xffff; i++) {

		/* for DfuSe devices we need to handle the address manually */
		if (priv->dfuse_supported) {

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
			if (!dfu_sector_has_cap (sector, DFU_SECTOR_CAP_READABLE)) {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_DEVICE,
					     "memory sector at 0x%04x is not readble",
					     (guint) offset);
				return FALSE;
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
		g_debug ("got #%04x chunk of size %li", i, chunk_size);
		g_ptr_array_add (chunks, chunk_tmp);

		/* update UI */
		if (progress_cb != NULL && chunk_size > 0) {
			progress_cb (DFU_STATE_DFU_UPLOAD_IDLE,
				     total_size,
				     expected_size,
				     progress_cb_data);
		}

		/* detect short write as EOF */
		if (chunk_size < priv->transfer_size)
			break;
	}

	/* check final size */
	if (expected_size > 0) {
		if (total_size != expected_size) {
			g_set_error (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_FILE,
				     "invalid size, got %li, expected %li",
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
 * @progress_cb: a #GFileProgressCallback, or %NULL
 * @progress_cb_data: user data to pass to @progress_cb
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
		   DfuProgressCallback progress_cb,
		   gpointer progress_cb_data,
		   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	guint i;
	guint32 last_sector_id = G_MAXUINT;
	g_autoptr(DfuImage) image = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* can the target do this? */
	if (!dfu_target_can_upload (target)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "target cannot do uploading");
		return NULL;
	}

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
	dfu_image_set_name (image, priv->iface_alt_setting_name);
	dfu_image_set_alt_setting (image, priv->iface_alt_setting);

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
						     progress_cb,
						     progress_cb_data,
						     error);
		if (element == NULL)
			return NULL;

		/* this element was uploaded okay */
		dfu_image_add_element (image, element);

		/* ignore sectors until one of these changes */
		last_sector_id = dfu_sector_get_id (sector);
	}

	/* do host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_HOST_RESET) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) > 0) {
		if (!dfu_device_reset (priv->device, error))
			return NULL;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) {
		g_debug ("booting to runtime");
		if (!dfu_device_wait_for_replug (priv->device, 2000, cancellable, error))
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
		return g_strdup_printf ("got %li bytes, expected %li",
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
			     DfuProgressCallback progress_cb,
			     gpointer progress_cb_data,
			     GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	GBytes *bytes;
	guint i;
	guint nr_chunks;
	guint dfuse_sector_offset = 0;
	guint last_sector_id = G_MAXUINT;
	g_autoptr(GError) error_local = NULL;

	/* ST uses wBlockNum=0 for DfuSe commands and wBlockNum=1 is reserved */
	if (priv->dfuse_supported)
		dfuse_sector_offset = 2;

	/* round up as we have to transfer incomplete blocks */
	bytes = dfu_element_get_contents (element);
	nr_chunks = ceil ((gdouble) g_bytes_get_size (bytes) /
			  (gdouble) priv->transfer_size);
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
		g_autoptr(GBytes) bytes_tmp = NULL;

		/* caclulate the offset into the element data */
		offset = i * priv->transfer_size;

		/* for DfuSe devices we need to handle the erase and setting
		 * the address manually */
		if (priv->dfuse_supported) {

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
			if (length > priv->transfer_size)
				length = priv->transfer_size;
			bytes_tmp = g_bytes_new_from_bytes (bytes, offset, length);
		} else {
			bytes_tmp = g_bytes_new (NULL, 0);
		}
		g_debug ("writing #%04x chunk of size %li",
			 i, g_bytes_get_size (bytes_tmp));
		if (!dfu_target_download_chunk (target,
						i + dfuse_sector_offset,
						bytes_tmp,
						cancellable,
						error))
			return FALSE;

		/* update UI */
		if (progress_cb != NULL) {
			progress_cb (DFU_STATE_DFU_DNLOAD_IDLE,
				     offset,
				     g_bytes_get_size (bytes),
				     progress_cb_data);
		}

		/* give the target a chance to update */
		g_usleep (priv->dnload_timeout * 1000);

		/* getting the status moves the state machine to DNLOAD-IDLE */
		if (!dfu_target_refresh (target, cancellable, error))
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
							 progress_cb,
							 progress_cb_data,
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
 * @progress_cb: a #GFileProgressCallback, or %NULL
 * @progress_cb_data: user data to pass to @progress_cb
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
		     DfuProgressCallback progress_cb,
		     gpointer progress_cb_data,
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

	/* can the target do this? */
	if (!dfu_target_can_download (target)) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_SUPPORTED,
				     "target cannot do downloading");
		return FALSE;
	}

	/* mark these as all erased */
	if (priv->dfuse_supported)
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
						   progress_cb,
						   progress_cb_data,
						   error);
		if (!ret)
			return FALSE;
	}

	/* do a host reset */
	if ((flags & DFU_TARGET_TRANSFER_FLAG_HOST_RESET) > 0 ||
	    (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) > 0) {
		if (!dfu_device_reset (priv->device, error))
			return FALSE;
	}

	/* boot to runtime */
	if (flags & DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME) {
		g_debug ("booting to runtime to set auto-boot");
		if (!dfu_device_wait_for_replug (priv->device, 2000, cancellable, error))
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
	if (!priv->dfuse_supported) {
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
 * dfu_target_set_timeout:
 * @target: a #DfuTarget
 * @timeout_ms: the timeout in ms
 *
 * Sets the USB timeout to use when contacting the USB target.
 *
 * Since: 0.5.4
 **/
void
dfu_target_set_timeout (DfuTarget *target, guint timeout_ms)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_if_fail (DFU_IS_TARGET (target));
	priv->timeout_ms = timeout_ms;
}

/**
 * dfu_target_get_interface_number:
 * @target: a #DfuTarget
 *
 * Gets the interface number.
 *
 * Since: 0.5.4
 **/
guint8
dfu_target_get_interface_number (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0xff);
	return priv->iface_number;
}

/**
 * dfu_target_get_interface_alt_setting:
 * @target: a #DfuTarget
 *
 * Gets the alternate setting to use for this interface.
 *
 * Return value: the alternative setting, typically zero
 *
 * Since: 0.5.4
 **/
guint8
dfu_target_get_interface_alt_setting (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), 0xff);
	return priv->iface_alt_setting;
}

/**
 * dfu_target_get_interface_alt_name:
 * @target: a #DfuTarget
 *
 * Gets the alternate setting name to use for this interface.
 *
 * Return value: the alternative setting name, typically %NULL
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_target_get_interface_alt_name (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	return priv->iface_alt_setting_name;
}
