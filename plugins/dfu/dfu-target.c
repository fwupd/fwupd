/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
#include "dfu-device.h"
#include "dfu-sector.h"
#include "dfu-target-private.h"

#include "fwupd-error.h"

static void dfu_target_finalize			 (GObject *object);

typedef struct {
	DfuDevice		*device;		/* not refcounted */
	gboolean		 done_setup;
	guint8			 alt_setting;
	guint8			 alt_idx;
	gchar			*alt_name;
	gchar			*alt_name_for_display;
	GPtrArray		*sectors;		/* of DfuSector */
	guint			 old_percentage;
	FwupdStatus		 old_action;
} DfuTargetPrivate;

enum {
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_ACTION_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (DfuTarget, dfu_target, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_target_get_instance_private (o))

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
	 **/
	signals [SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuTargetClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * DfuTarget::action-changed:
	 * @device: the #DfuTarget instance that emitted the signal
	 * @action: the new FwupdStatus
	 *
	 * The ::action-changed signal is emitted when the high level action changes.
	 **/
	signals [SIGNAL_ACTION_CHANGED] =
		g_signal_new ("action-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuTargetClass, action_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	object_class->finalize = dfu_target_finalize;
}

static void
dfu_target_init (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	priv->sectors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->old_percentage = G_MAXUINT;
	priv->old_action = FWUPD_STATUS_IDLE;
}

static void
dfu_target_finalize (GObject *object)
{
	DfuTarget *target = DFU_TARGET (object);
	DfuTargetPrivate *priv = GET_PRIVATE (target);

	g_free (priv->alt_name);
	g_free (priv->alt_name_for_display);
	g_ptr_array_unref (priv->sectors);

	/* we no longer care */
	if (priv->device != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (priv->device),
					      (gpointer *) &priv->device);
	}

	G_OBJECT_CLASS (dfu_target_parent_class)->finalize (object);
}

static gchar *
dfu_target_sectors_to_string (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GString *str = g_string_new ("");
	for (guint i = 0; i < priv->sectors->len; i++) {
		DfuSector *sector = g_ptr_array_index (priv->sectors, i);
		g_autofree gchar *tmp = dfu_sector_to_string (sector);
		g_string_append_printf (str, "%s\n", tmp);
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

DfuSector *
dfu_target_get_sector_for_addr (DfuTarget *target, guint32 addr)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);

	for (guint i = 0; i < priv->sectors->len; i++) {
		DfuSector *sector = g_ptr_array_index (priv->sectors, i);
		if (addr < dfu_sector_get_address (sector))
			continue;
		if (addr > dfu_sector_get_address (sector) +
				dfu_sector_get_size (sector))
			continue;
		return sector;
	}
	return NULL;
}

static gboolean
dfu_target_parse_sector (DfuTarget *target,
			 const gchar *dfuse_sector_id,
			 guint32 *addr,
			 guint16 zone,
			 guint16 number,
			 GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSectorCap cap = DFU_SECTOR_CAP_NONE;
	gchar *tmp;
	guint32 addr_offset = 0;
	guint64 nr_sectors;
	guint64 sector_size;

	/* parse # of sectors */
	nr_sectors = g_ascii_strtoull (dfuse_sector_id, &tmp, 10);
	if (nr_sectors > 999) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Invalid number of sectors: %s",
			     dfuse_sector_id);
		return FALSE;
	}

	/* check this is the delimiter */
	if (tmp[0] != '*') {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Invalid sector ID: %s",
			     dfuse_sector_id);
		return FALSE;
	}

	/* parse sector size */
	sector_size = g_ascii_strtoull (tmp + 1, &tmp, 10);
	if (sector_size > 999) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Invalid sector size: %s",
			     dfuse_sector_id);
		return FALSE;
	}

	/* get multiplier */
	switch (tmp[0]) {
	case 'B':		/* byte */
	case ' ':		/* byte, ST reference bootloader :/ */
		break;
	case 'K':		/* Kilo */
		sector_size *= 0x400;
		break;
	case 'M':		/* Mega */
		sector_size *= 0x100000	;
		break;
	default:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
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
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Invalid sector type: %s",
			     tmp);
		return FALSE;
	}

	/* add all the sectors */
	for (guint i = 0; i < nr_sectors; i++) {
		DfuSector *sector;
		sector = dfu_sector_new (*addr + addr_offset,
					 (guint32) sector_size,
					 (guint32) ((nr_sectors * sector_size) - addr_offset),
					 zone,
					 number,
					 cap);
		g_ptr_array_add (priv->sectors, sector);
		addr_offset += dfu_sector_get_size (sector);
	}

	/* update for next sector */
	*addr += addr_offset;
	return TRUE;
}

gboolean
dfu_target_parse_sectors (DfuTarget *target, const gchar *alt_name, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_autofree gchar *str_debug = NULL;
	g_auto(GStrv) zones = NULL;

	/* not set */
	if (alt_name == NULL)
		return TRUE;

	/* From the Neo Freerunner */
	if (g_str_has_prefix (alt_name, "RAM 0x")) {
		DfuSector *sector;
		guint64 addr_tmp;
		addr_tmp = g_ascii_strtoull (alt_name + 6, NULL, 16);
		if (addr_tmp == 0 || addr_tmp > G_MAXUINT32)
			return FALSE;
		g_debug ("RAM description, so parsing");
		sector = dfu_sector_new ((guint32) addr_tmp,
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
	g_free (priv->alt_name_for_display);
	priv->alt_name_for_display = g_strdup (g_strchomp (zones[0] + 1));
	for (guint i = 1; zones[i] != NULL; i += 2) {
		guint32 addr;
		guint64 addr_tmp;
		g_auto(GStrv) sectors = NULL;

		/* parse address */
		if (!g_str_has_prefix (zones[i], "0x")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "No sector address");
			return FALSE;
		}
		addr_tmp = g_ascii_strtoull (zones[i] + 2, NULL, 16);
		if (addr_tmp > G_MAXUINT32) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Sector address too large");
			return FALSE;
		}
		addr = (guint32) addr_tmp;

		/* no sectors?! */
		if (zones[i+1] == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "No sector section");
			return FALSE;
		}

		/* parse sectors */
		sectors = g_strsplit (zones[i+1], ",", -1);
		for (guint16 j = 0; sectors[j] != NULL; j++) {
			if (!dfu_target_parse_sector (target,
						      sectors[j],
						      &addr,
						      (i - 1) / 2, j,
						      error)) {
				g_prefix_error (error,
						"Failed to parse: '%s': ",
						sectors[j]);
				return FALSE;
			}
		}
	}

	/* success */
	str_debug = dfu_target_sectors_to_string (target);
	g_debug ("%s", str_debug);
	return TRUE;
}

/**
 * dfu_target_new: (skip)
 *
 * Creates a new DFU target, which represents an alt-setting on a
 * DFU-capable device.
 *
 * Return value: a #DfuTarget
 **/
DfuTarget *
dfu_target_new (void)
{
	DfuTarget *target;
	target = g_object_new (DFU_TYPE_TARGET, NULL);
	return target;
}

/**
 * dfu_target_get_sectors:
 * @target: a #GUsbDevice
 *
 * Gets the sectors exported by the target.
 *
 * Return value: (transfer none) (element-type DfuSector): sectors
 **/
GPtrArray *
dfu_target_get_sectors (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	return priv->sectors;
}

/**
 * dfu_target_get_sector_default:
 * @target: a #GUsbDevice
 *
 * Gets the default (first) sector exported by the target.
 *
 * Return value: (transfer none): a #DfuSector, or %NULL
 **/
DfuSector *
dfu_target_get_sector_default (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	if (priv->sectors->len == 0)
		return NULL;
	return DFU_SECTOR (g_ptr_array_index (priv->sectors, 0));
}

/**
 * dfu_target_status_to_error_msg:
 * @status: a #DfuStatus, e.g. %DFU_STATUS_ERR_ERASE
 *
 * Converts an enumerated value to an error description.
 *
 * Return value: a string
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

gboolean
dfu_target_check_status (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuStatus status;

	/* get the status */
	if (!dfu_device_refresh (priv->device, error))
		return FALSE;

	/* wait for dfuDNBUSY to not be set */
	if (dfu_device_get_version (priv->device) == DFU_VERSION_DFUSE) {
		while (dfu_device_get_state (priv->device) == DFU_STATE_DFU_DNBUSY) {
			g_debug ("waiting for DFU_STATE_DFU_DNBUSY to clear");
			g_usleep (dfu_device_get_download_timeout (priv->device) * 1000);
			if (!dfu_device_refresh (priv->device, error))
				return FALSE;
		}
	}

	/* not in an error state */
	if (dfu_device_get_state (priv->device) != DFU_STATE_DFU_ERROR)
		return TRUE;

	/* STM32-specific long errors */
	status = dfu_device_get_status (priv->device);
	if (dfu_device_get_version (priv->device) == DFU_VERSION_DFUSE) {
		if (status == DFU_STATUS_ERR_VENDOR) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Read protection is active");
			return FALSE;
		}
		if (status == DFU_STATUS_ERR_TARGET) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Address is wrong or unsupported");
			return FALSE;
		}
	}

	/* use a proper error description */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
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
 **/
static gboolean
dfu_target_use_alt_setting (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (priv->device));
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure interface is claimed */
	if (!dfu_device_ensure_interface (priv->device, error))
		return FALSE;

	/* use the correct setting */
	if (fu_device_has_flag (FU_DEVICE (priv->device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!g_usb_device_set_interface_alt (usb_device,
						     (gint) dfu_device_get_interface (priv->device),
						     (gint) priv->alt_setting,
						     &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "cannot set alternate setting 0x%02x on interface %i: %s",
				     priv->alt_setting,
				     dfu_device_get_interface (priv->device),
				     error_local->message);
			return FALSE;
		}
	}

	return TRUE;
}

void
dfu_target_set_alt_name (DfuTarget *target, const gchar *alt_name)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_free (priv->alt_name);
	priv->alt_name = g_strdup (alt_name);
}

void
dfu_target_set_device (DfuTarget *target, DfuDevice *device)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_set_object (&priv->device, device);

	/* if we try to ref the target and destroy the device */
	g_object_add_weak_pointer (G_OBJECT (priv->device),
				   (gpointer *) &priv->device);
}

/**
 * dfu_target_setup:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable target.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_target_setup (DfuTarget *target, GError **error)
{
	DfuTargetClass *klass = DFU_TARGET_GET_CLASS (target);
	DfuTargetPrivate *priv = GET_PRIVATE (target);

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->done_setup)
		return TRUE;

	/* superclassed */
	if (klass->setup != NULL) {
		if (!klass->setup (target, error))
			return FALSE;
	}

	/* get string */
	if (priv->alt_idx != 0x00 && priv->alt_name == NULL) {
		GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (priv->device));
		priv->alt_name =
			g_usb_device_get_string_descriptor (usb_device,
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
		g_debug ("no UM0424 sector description in %s", priv->alt_name);
		g_ptr_array_add (priv->sectors, sector);
	}

	priv->done_setup = TRUE;
	return TRUE;
}

/**
 * dfu_target_mass_erase:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Mass erases the device clearing all SRAM and EEPROM memory.
 *
 * IMPORTANT: This only works on STM32 devices from ST and AVR32 devices from Atmel.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_target_mass_erase (DfuTarget *target, GError **error)
{
	DfuTargetClass *klass = DFU_TARGET_GET_CLASS (target);
	if (!dfu_target_setup (target, error))
		return FALSE;
	if (klass->mass_erase == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "mass erase not supported");
		return FALSE;
	}
	return klass->mass_erase (target, error);
}

gboolean
dfu_target_download_chunk (DfuTarget *target, guint16 index, GBytes *bytes, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (priv->device));
	g_autoptr(GError) error_local = NULL;
	gsize actual_length;

	/* low level packet debugging */
	if (g_getenv ("FWUPD_DFU_VERBOSE") != NULL) {
		gsize sz = 0;
		const guint8 *data = g_bytes_get_data (bytes, &sz);
		for (gsize i = 0; i < sz; i++)
			g_print ("Message: m[%" G_GSIZE_FORMAT "] = 0x%02x\n", i, (guint) data[i]);
	}

	if (!g_usb_device_control_transfer (usb_device,
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
					    NULL,
					    &error_local)) {
		/* refresh the error code */
		dfu_device_error_fixup (priv->device, &error_local);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot download data: %s",
			     error_local->message);
		return FALSE;
	}

	/* for STM32 devices, the action only occurs when we do GetStatus */
	if (dfu_device_get_version (priv->device) == DFU_VERSION_DFUSE) {
		if (!dfu_device_refresh (priv->device, error))
			return FALSE;
	}

	/* wait for the device to write contents to the EEPROM */
	if (g_bytes_get_size (bytes) == 0 &&
	    dfu_device_get_download_timeout (priv->device) > 0) {
		dfu_target_set_action (target, FWUPD_STATUS_IDLE);
		dfu_target_set_action (target, FWUPD_STATUS_DEVICE_BUSY);
	}
	if (dfu_device_get_download_timeout (priv->device) > 0) {
		g_debug ("sleeping for %ums…",
			 dfu_device_get_download_timeout (priv->device));
		g_usleep (dfu_device_get_download_timeout (priv->device) * 1000);
	}

	/* find out if the write was successful */
	if (!dfu_device_refresh (priv->device, error))
		return FALSE;

	g_assert (actual_length == g_bytes_get_size (bytes));
	return TRUE;
}

GBytes *
dfu_target_upload_chunk (DfuTarget *target, guint16 index, gsize buf_sz, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (priv->device));
	g_autoptr(GError) error_local = NULL;
	guint8 *buf;
	gsize actual_length;

	/* unset */
	if (buf_sz == 0)
		buf_sz = (gsize) dfu_device_get_transfer_size (priv->device);

	buf = g_new0 (guint8, buf_sz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    DFU_REQUEST_UPLOAD,
					    index,
					    dfu_device_get_interface (priv->device),
					    buf, buf_sz,
					    &actual_length,
					    dfu_device_get_timeout (priv->device),
					    NULL,
					    &error_local)) {
		/* refresh the error code */
		dfu_device_error_fixup (priv->device, &error_local);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot upload data: %s",
			     error_local->message);
		return NULL;
	}

	/* low level packet debugging */
	if (g_getenv ("FWUPD_DFU_VERBOSE") != NULL) {
		for (gsize i = 0; i < actual_length; i++)
			g_print ("Message: r[%" G_GSIZE_FORMAT "] = 0x%02x\n", i, (guint) buf[i]);
	}

	return g_bytes_new_take (buf, actual_length);
}

void
dfu_target_set_alt_idx (DfuTarget *target, guint8 alt_idx)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	priv->alt_idx = alt_idx;
}

void
dfu_target_set_alt_setting (DfuTarget *target, guint8 alt_setting)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	priv->alt_setting = alt_setting;
}

void
dfu_target_set_action (DfuTarget *target, FwupdStatus action)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);

	/* unchanged */
	if (priv->old_action == action)
		return;
	if (priv->old_action != FWUPD_STATUS_IDLE &&
	    action != FWUPD_STATUS_IDLE) {
		g_debug ("ignoring action %s as %s already set and not idle",
			 fwupd_status_to_string (action),
			 fwupd_status_to_string (priv->old_action));
		return;
	}
	g_debug ("setting action %s", fwupd_status_to_string (action));
	g_signal_emit (target, signals[SIGNAL_ACTION_CHANGED], 0, action);
	priv->old_action = action;
}

DfuDevice *
dfu_target_get_device (DfuTarget *target)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	return priv->device;
}

void
dfu_target_set_percentage_raw (DfuTarget *target, guint percentage)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	if (percentage == priv->old_percentage)
		return;
	g_debug ("setting percentage %u%% of %s",
		 percentage, fwupd_status_to_string (priv->old_action));
	g_signal_emit (target,
		       signals[SIGNAL_PERCENTAGE_CHANGED],
		       0, percentage);
	priv->old_percentage = percentage;
}

void
dfu_target_set_percentage (DfuTarget *target, guint value, guint total)
{
	guint percentage;

	g_return_if_fail (total > 0);

	percentage = (value * 100) / total;
	if (percentage >= 100)
		return;
	dfu_target_set_percentage_raw (target, percentage);
}

gboolean
dfu_target_attach (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuTargetClass *klass = DFU_TARGET_GET_CLASS (target);

	/* ensure populated */
	if (!dfu_target_setup (target, error))
		return FALSE;

	/* implemented as part of a superclass */
	if (klass->attach != NULL)
		return klass->attach (target, error);

	/* normal DFU mode just needs a bus reset */
	return dfu_device_reset (priv->device, error);
}

static DfuElement *
dfu_target_upload_element_dfu (DfuTarget *target,
			       guint32 address,
			       gsize expected_size,
			       gsize maximum_size,
			       GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuElement *element = NULL;
	GBytes *chunk_tmp;
	guint32 offset = 0;
	guint percentage_size = expected_size > 0 ? expected_size : maximum_size;
	gsize total_size = 0;
	guint16 transfer_size = dfu_device_get_transfer_size (priv->device);
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* update UI */
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_READ);

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint16 idx = 0; idx < G_MAXUINT16; idx++) {
		guint32 chunk_size;

		/* read chunk of data */
		chunk_tmp = dfu_target_upload_chunk (target,
						     idx,
						     0, /* device transfer size */
						     error);
		if (chunk_tmp == NULL)
			return NULL;

		/* keep a sum of all the chunks */
		chunk_size = (guint32) g_bytes_get_size (chunk_tmp);
		total_size += chunk_size;
		offset += chunk_size;

		/* add to array */
		g_debug ("got #%04x chunk of size %" G_GUINT32_FORMAT,
			 idx, chunk_size);
		g_ptr_array_add (chunks, chunk_tmp);

		/* update UI */
		if (chunk_size > 0)
			dfu_target_set_percentage (target, total_size, percentage_size);

		/* detect short write as EOF */
		if (chunk_size < transfer_size)
			break;
	}

	/* check final size */
	if (expected_size > 0) {
		if (total_size != expected_size) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid size, got %" G_GSIZE_FORMAT ", "
				     "expected %" G_GSIZE_FORMAT ,
				     total_size, expected_size);
			return NULL;
		}
	}

	/* done */
	dfu_target_set_percentage_raw (target, 100);
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* create new image */
	contents = dfu_utils_bytes_join_array (chunks);
	element = dfu_element_new ();
	dfu_element_set_contents (element, contents);
	return element;
}

static DfuElement *
dfu_target_upload_element (DfuTarget *target,
			   guint32 address,
			   gsize expected_size,
			   gsize maximum_size,
			   GError **error)
{
	DfuTargetClass *klass = DFU_TARGET_GET_CLASS (target);

	/* implemented as part of a superclass */
	if (klass->upload_element != NULL) {
		return klass->upload_element (target, address, expected_size,
					      maximum_size, error);
	}
	return dfu_target_upload_element_dfu (target,
					      address,
					      expected_size,
					      maximum_size,
					      error);
}

static guint32
dfu_target_get_size_of_zone (DfuTarget *target, guint16 zone)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	guint32 len = 0;
	for (guint i = 0; i < priv->sectors->len; i++) {
		DfuSector *sector = g_ptr_array_index (priv->sectors, i);
		if (dfu_sector_get_zone (sector) != zone)
			continue;
		len += dfu_sector_get_size (sector);
	}
	return len;
}

/**
 * dfu_target_upload:
 * @target: a #DfuTarget
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @error: a #GError, or %NULL
 *
 * Uploads firmware from the target to the host.
 *
 * Return value: (transfer full): the uploaded image, or %NULL for error
 **/
DfuImage *
dfu_target_upload (DfuTarget *target,
		   DfuTargetTransferFlags flags,
		   GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuSector *sector;
	guint16 zone_cur;
	guint32 zone_size = 0;
	guint32 zone_last = G_MAXUINT;
	g_autoptr(DfuImage) image = NULL;

	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure populated */
	if (!dfu_target_setup (target, error))
		return NULL;

	/* can the target do this? */
	if (!dfu_device_can_upload (priv->device)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "target cannot do uploading");
		return NULL;
	}

	/* use correct alt */
	if (!dfu_target_use_alt_setting (target, error))
		return NULL;

	/* no open?! */
	if (priv->sectors->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no sectors defined for target");
		return NULL;
	}

	/* create a new image */
	image = dfu_image_new ();
	dfu_image_set_name (image, priv->alt_name);
	dfu_image_set_alt_setting (image, priv->alt_setting);

	/* get all the sectors for the device */
	for (guint i = 0; i < priv->sectors->len; i++) {
		g_autoptr(DfuElement) element = NULL;

		/* only upload to the start of any zone:sector */
		sector = g_ptr_array_index (priv->sectors, i);
		zone_cur = dfu_sector_get_zone (sector);
		if (zone_cur == zone_last)
			continue;

		/* get the size of the entire continuous zone */
		zone_size = dfu_target_get_size_of_zone (target, zone_cur);
		zone_last = zone_cur;

		/* get the first element from the hardware */
		g_debug ("starting upload from 0x%08x (0x%04x)",
			 dfu_sector_get_address (sector),
			 zone_size);
		element = dfu_target_upload_element (target,
						     dfu_sector_get_address (sector),
						     0,		/* expected */
						     zone_size,	/* maximum */
						     error);
		if (element == NULL)
			return NULL;

		/* this element was uploaded okay */
		dfu_image_add_element (image, element);
	}

	/* success */
	return g_object_ref (image);
}

static gchar *
_g_bytes_compare_verbose (GBytes *bytes1, GBytes *bytes2)
{
	const guint8 *data1;
	const guint8 *data2;
	gsize length1;
	gsize length2;

	data1 = g_bytes_get_data (bytes1, &length1);
	data2 = g_bytes_get_data (bytes2, &length2);

	/* not the same length */
	if (length1 != length2) {
		return g_strdup_printf ("got %" G_GSIZE_FORMAT " bytes, "
					"expected %" G_GSIZE_FORMAT,
					length1, length2);
	}

	/* return 00 01 02 03 */
	for (guint i = 0; i < length1; i++) {
		if (data1[i] != data2[i]) {
			return g_strdup_printf ("got 0x%02x, expected 0x%02x @ 0x%04x",
						data1[i], data2[i], i);
		}
	}
	return NULL;
}

static gboolean
dfu_target_download_element_dfu (DfuTarget *target,
				 DfuElement *element,
				 DfuTargetTransferFlags flags,
				 GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GBytes *bytes;
	guint16 nr_chunks;
	guint16 transfer_size = dfu_device_get_transfer_size (priv->device);

	/* round up as we have to transfer incomplete blocks */
	bytes = dfu_element_get_contents (element);
	nr_chunks = (guint) ceil ((gdouble) g_bytes_get_size (bytes) /
				  (gdouble) transfer_size);
	if (nr_chunks == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "zero-length firmware");
		return FALSE;
	}
	dfu_target_set_action (target, FWUPD_STATUS_DEVICE_WRITE);
	for (guint16 i = 0; i < nr_chunks + 1; i++) {
		gsize length;
		guint32 offset;
		g_autoptr(GBytes) bytes_tmp = NULL;

		/* caclulate the offset into the element data */
		offset = i * transfer_size;

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
		if (!dfu_target_download_chunk (target, i, bytes_tmp, error))
			return FALSE;

		/* update UI */
		dfu_target_set_percentage (target, offset, g_bytes_get_size (bytes));
	}

	/* done */
	dfu_target_set_percentage_raw (target, 100);
	dfu_target_set_action (target, FWUPD_STATUS_IDLE);

	/* success */
	return TRUE;
}

static gboolean
dfu_target_download_element (DfuTarget *target,
			     DfuElement *element,
			     DfuTargetTransferFlags flags,
			     GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	DfuTargetClass *klass = DFU_TARGET_GET_CLASS (target);

	/* implemented as part of a superclass */
	if (klass->download_element != NULL) {
		if (!klass->download_element (target, element, flags, error))
			return FALSE;
	} else {
		if (!dfu_target_download_element_dfu (target,
						      element,
						      flags,
						      error))
			return FALSE;
	}

	/* verify */
	if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY &&
	    dfu_device_has_attribute (priv->device, DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD)) {
		GBytes *bytes;
		GBytes *bytes_tmp;
		g_autoptr(DfuElement) element_tmp = NULL;
		dfu_target_set_action (target, FWUPD_STATUS_DEVICE_VERIFY);
		bytes = dfu_element_get_contents (element);
		element_tmp = dfu_target_upload_element (target,
							 dfu_element_get_address (element),
							 g_bytes_get_size (bytes),
							 g_bytes_get_size (bytes),
							 error);
		if (element_tmp == NULL)
			return FALSE;
		bytes_tmp = dfu_element_get_contents (element_tmp);
		if (g_bytes_compare (bytes_tmp, bytes) != 0) {
			g_autofree gchar *bytes_cmp_str = NULL;
			bytes_cmp_str = _g_bytes_compare_verbose (bytes_tmp, bytes);
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "verify failed: %s",
				     bytes_cmp_str);
			return FALSE;
		}
		dfu_target_set_action (target, FWUPD_STATUS_IDLE);
	}

	return TRUE;
}

/**
 * dfu_target_download:
 * @target: a #DfuTarget
 * @image: a #DfuImage
 * @flags: flags to use, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @error: a #GError, or %NULL
 *
 * Downloads firmware from the host to the target, optionally verifying
 * the transfer.
 *
 * Return value: %TRUE for success
 **/
gboolean
dfu_target_download (DfuTarget *target, DfuImage *image,
		     DfuTargetTransferFlags flags, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	GPtrArray *elements;
	gboolean ret;

	g_return_val_if_fail (DFU_IS_TARGET (target), FALSE);
	g_return_val_if_fail (DFU_IS_IMAGE (image), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure populated */
	if (!dfu_target_setup (target, error))
		return FALSE;

	/* can the target do this? */
	if (!dfu_device_can_download (priv->device)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "target cannot do downloading");
		return FALSE;
	}

	/* use correct alt */
	if (!dfu_target_use_alt_setting (target, error))
		return FALSE;

	/* download all elements in the image to the device */
	elements = dfu_image_get_elements (image);
	if (elements->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no image elements");
		return FALSE;
	}
	for (guint i = 0; i < elements->len; i++) {
		DfuElement *element = dfu_image_get_element (image, (guint8) i);
		g_debug ("downloading element at 0x%04x",
			 dfu_element_get_address (element));

		/* auto-detect missing firmware address -- this assumes
		 * that the first target is the main program memory and that
		 * there is only one element in the firmware file */
		if (flags & DFU_TARGET_TRANSFER_FLAG_ADDR_HEURISTIC &&
		    dfu_element_get_address (element) == 0x0 &&
		    elements->len == 1 &&
		    priv->sectors->len > 0) {
			DfuSector *sector = g_ptr_array_index (priv->sectors, 0);
			g_debug ("fixing up firmware address from 0x0 to 0x%x",
				 dfu_sector_get_address (sector));
			dfu_element_set_address (element, dfu_sector_get_address (sector));
		}

		/* download to device */
		ret = dfu_target_download_element (target,
						   element,
						   flags,
						   error);
		if (!ret)
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * dfu_target_get_alt_setting:
 * @target: a #DfuTarget
 *
 * Gets the alternate setting to use for this interface.
 *
 * Return value: the alternative setting, typically zero
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
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no alt-name");
		return NULL;
	}

	return priv->alt_name;
}

/**
 * dfu_target_get_alt_name_for_display:
 * @target: a #DfuTarget
 * @error: a #GError, or %NULL
 *
 * Gets the alternate setting name to use for this interface that can be
 * shown on the display.
 *
 * Return value: the alternative setting name
 **/
const gchar *
dfu_target_get_alt_name_for_display (DfuTarget *target, GError **error)
{
	DfuTargetPrivate *priv = GET_PRIVATE (target);
	g_return_val_if_fail (DFU_IS_TARGET (target), NULL);

	/* ensure populated */
	if (!dfu_target_setup (target, error))
		return NULL;

	/* nothing */
	if (priv->alt_name_for_display == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no alt-name for display");
		return NULL;
	}

	return priv->alt_name_for_display;
}
