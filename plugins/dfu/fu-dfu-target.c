/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * FuDfuTarget:
 *
 * This object allows uploading and downloading an image onto a
 * specific DFU-capable target.
 *
 * You only need to use this in preference to #FuDfuDevice if you only
 * want to update one target on the device. Most users will want to
 * update all the targets on the device at the same time.
 *
 * See also: [class@FuDfuDevice], [class@FuFirmware]
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "fu-dfu-common.h"
#include "fu-dfu-device.h"
#include "fu-dfu-sector.h"
#include "fu-dfu-struct.h"
#include "fu-dfu-target-private.h" /* waive-pre-commit */

#define DFU_TARGET_MANIFEST_MAX_POLLING_TRIES 200

typedef struct {
	gboolean done_setup;
	guint8 alt_setting;
	guint8 alt_idx;
	GPtrArray *sectors; /* of FuDfuSector */
} FuDfuTargetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuDfuTarget, fu_dfu_target, FU_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_dfu_target_get_instance_private(o))

static void
fu_dfu_target_init(FuDfuTarget *self)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	priv->sectors = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_dfu_target_finalize(GObject *object)
{
	FuDfuTarget *self = FU_DFU_TARGET(object);
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);

	g_ptr_array_unref(priv->sectors);

	G_OBJECT_CLASS(fu_dfu_target_parent_class)->finalize(object);
}

static void
fu_dfu_target_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDfuTarget *self = FU_DFU_TARGET(device);
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	fu_string_append_kx(str, idt, "AltSetting", priv->alt_setting);
	fu_string_append_kx(str, idt, "AltIdx", priv->alt_idx);
	for (guint i = 0; i < priv->sectors->len; i++) {
		FuDfuSector *sector = g_ptr_array_index(priv->sectors, i);
		g_autofree gchar *tmp1 = g_strdup_printf("Idx%02x", i);
		g_autofree gchar *tmp2 = fu_dfu_sector_to_string(sector);
		fu_string_append(str, idt + 1, tmp1, tmp2);
	}
}

FuDfuSector *
fu_dfu_target_get_sector_for_addr(FuDfuTarget *self, guint32 addr)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);

	for (guint i = 0; i < priv->sectors->len; i++) {
		FuDfuSector *sector = g_ptr_array_index(priv->sectors, i);
		if (addr < fu_dfu_sector_get_address(sector))
			continue;
		if (addr > fu_dfu_sector_get_address(sector) + fu_dfu_sector_get_size(sector))
			continue;
		return sector;
	}
	return NULL;
}

static gboolean
fu_dfu_target_parse_sector(FuDfuTarget *self,
			   const gchar *dfuse_sector_id,
			   guint32 *addr,
			   guint16 zone,
			   guint16 number,
			   GError **error)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	FuDfuSectorCap cap = FU_DFU_SECTOR_CAP_NONE;
	gchar *tmp;
	guint32 addr_offset = 0;
	guint64 nr_sectors;
	guint64 sector_size;

	/* parse # of sectors */
	nr_sectors = g_ascii_strtoull(dfuse_sector_id, &tmp, 10);
	if (nr_sectors > 999) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Invalid number of sectors: %s",
			    dfuse_sector_id);
		return FALSE;
	}

	/* check this is the delimiter */
	if (tmp[0] != '*') {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Invalid sector ID: %s",
			    dfuse_sector_id);
		return FALSE;
	}

	/* parse sector size */
	sector_size = g_ascii_strtoull(tmp + 1, &tmp, 10);
	if (sector_size > 999) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Invalid sector size: %s",
			    dfuse_sector_id);
		return FALSE;
	}

	/* handle weirdness */
	if (fu_device_has_private_flag(fu_device_get_proxy(FU_DEVICE(self)),
				       FU_DFU_DEVICE_FLAG_ABSENT_SECTOR_SIZE)) {
		if (tmp[1] == '\0') {
			tmp[1] = tmp[0];
			tmp[0] = 'B';
		}
	}

	/* get multiplier */
	switch (tmp[0]) {
	case 'B': /* byte */
	case ' ': /* byte, ST reference bootloader :/ */
		break;
	case 'K': /* Kilo */
		sector_size *= 0x400;
		break;
	case 'M': /* Mega */
		sector_size *= 0x100000;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Invalid sector multiplier: %s",
			    tmp);
		return FALSE;
	}

	/* get sector type */
	switch (tmp[1]) {
	case 'a':
		cap = FU_DFU_SECTOR_CAP_READABLE;
		break;
	case 'b':
		cap = FU_DFU_SECTOR_CAP_ERASABLE;
		break;
	case 'c':
		cap = FU_DFU_SECTOR_CAP_READABLE | FU_DFU_SECTOR_CAP_ERASABLE;
		break;
	case 'd':
		cap = FU_DFU_SECTOR_CAP_WRITEABLE;
		break;
	case 'e':
		cap = FU_DFU_SECTOR_CAP_READABLE | FU_DFU_SECTOR_CAP_WRITEABLE;
		break;
	case 'f':
		cap = FU_DFU_SECTOR_CAP_ERASABLE | FU_DFU_SECTOR_CAP_WRITEABLE;
		break;
	case 'g':
		cap = FU_DFU_SECTOR_CAP_READABLE | FU_DFU_SECTOR_CAP_ERASABLE |
		      FU_DFU_SECTOR_CAP_WRITEABLE;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Invalid sector type: %s",
			    tmp);
		return FALSE;
	}

	/* add all the sectors */
	for (guint i = 0; i < nr_sectors; i++) {
		FuDfuSector *sector;
		sector = fu_dfu_sector_new(*addr + addr_offset,
					   (guint32)sector_size,
					   (guint32)((nr_sectors * sector_size) - addr_offset),
					   zone,
					   number,
					   cap);
		g_ptr_array_add(priv->sectors, sector);
		addr_offset += fu_dfu_sector_get_size(sector);
	}

	/* update for next sector */
	*addr += addr_offset;
	return TRUE;
}

gboolean
fu_dfu_target_parse_sectors(FuDfuTarget *self, const gchar *alt_name, GError **error)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) zones = NULL;

	/* not set */
	if (alt_name == NULL)
		return TRUE;

	/* From the Neo Freerunner */
	if (g_str_has_prefix(alt_name, "RAM 0x")) {
		FuDfuSector *sector;
		guint64 addr_tmp;
		addr_tmp = g_ascii_strtoull(alt_name + 6, NULL, 16);
		if (addr_tmp == 0 || addr_tmp > G_MAXUINT32)
			return FALSE;
		g_debug("RAM description, so parsing");
		sector = fu_dfu_sector_new((guint32)addr_tmp,
					   0x0, /* size */
					   0x0, /* size_left */
					   0x0, /* zone */
					   0x0, /* number */
					   FU_DFU_SECTOR_CAP_ERASABLE | FU_DFU_SECTOR_CAP_READABLE |
					       FU_DFU_SECTOR_CAP_WRITEABLE);
		g_ptr_array_add(priv->sectors, sector);
	}

	/* not a DfuSe alternative name */
	if (alt_name[0] != '@')
		return TRUE;

	/* clear any existing zones */
	g_ptr_array_set_size(priv->sectors, 0);

	/* parse zones */
	zones = g_strsplit(alt_name, "/", -1);
	fu_device_set_name(FU_DEVICE(self), g_strchomp(zones[0] + 1));
	for (guint i = 1; zones[i] != NULL; i += 2) {
		guint32 addr;
		guint64 addr_tmp;
		g_auto(GStrv) sectors = NULL;

		/* parse address */
		if (!g_str_has_prefix(zones[i], "0x")) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No sector address");
			return FALSE;
		}
		addr_tmp = g_ascii_strtoull(zones[i] + 2, NULL, 16);
		if (addr_tmp > G_MAXUINT32) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Sector address too large");
			return FALSE;
		}
		addr = (guint32)addr_tmp;

		/* no sectors?! */
		if (zones[i + 1] == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No sector section");
			return FALSE;
		}

		/* parse sectors */
		sectors = g_strsplit(zones[i + 1], ",", -1);
		for (guint16 j = 0; sectors[j] != NULL; j++) {
			if (!fu_dfu_target_parse_sector(self,
							sectors[j],
							&addr,
							(i - 1) / 2,
							j,
							error)) {
				g_prefix_error(error, "Failed to parse: '%s': ", sectors[j]);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_dfu_target_new: (skip)
 *
 * Creates a new DFU target, which represents an alt-setting on a
 * DFU-capable device.
 *
 * Returns: a #FuDfuTarget
 **/
FuDfuTarget *
fu_dfu_target_new(void)
{
	FuDfuTarget *self;
	self = g_object_new(FU_TYPE_DFU_TARGET, NULL);
	return self;
}

/**
 * fu_dfu_target_get_sectors:
 * @self: a #FuDfuTarget
 *
 * Gets the sectors exported by the target.
 *
 * Returns: (transfer none) (element-type FuDfuSector): sectors
 **/
GPtrArray *
fu_dfu_target_get_sectors(FuDfuTarget *self)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_TARGET(self), NULL);
	return priv->sectors;
}

/**
 * fu_dfu_target_get_sector_default:
 * @self: a #FuDfuTarget
 *
 * Gets the default (first) sector exported by the target.
 *
 * Returns: (transfer none): a #FuDfuSector, or %NULL
 **/
FuDfuSector *
fu_dfu_target_get_sector_default(FuDfuTarget *self)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_TARGET(self), NULL);
	if (priv->sectors->len == 0)
		return NULL;
	return FU_DFU_SECTOR(g_ptr_array_index(priv->sectors, 0));
}

/**
 * fu_dfu_target_status_to_error_msg:
 * @status: a #FuDfuStatus, e.g. %FU_DFU_STATUS_ERR_ERASE
 *
 * Converts an enumerated value to an error description.
 *
 * Returns: a string
 **/
static const gchar *
fu_dfu_target_status_to_error_msg(FuDfuStatus status)
{
	if (status == FU_DFU_STATUS_OK)
		return "No error condition is present";
	if (status == FU_DFU_STATUS_ERR_TARGET)
		return "Firmware is not for designed this device";
	if (status == FU_DFU_STATUS_ERR_FILE)
		return "Firmware is for this device but fails verification";
	if (status == FU_DFU_STATUS_ERR_WRITE)
		return "Device is unable to write memory";
	if (status == FU_DFU_STATUS_ERR_ERASE)
		return "Memory erase function failed";
	if (status == FU_DFU_STATUS_ERR_CHECK_ERASED)
		return "Memory erase check failed";
	if (status == FU_DFU_STATUS_ERR_PROG)
		return "Program memory function failed";
	if (status == FU_DFU_STATUS_ERR_VERIFY)
		return "Programmed memory failed verification";
	if (status == FU_DFU_STATUS_ERR_ADDRESS)
		return "Cannot program memory due to address out of range";
	if (status == FU_DFU_STATUS_ERR_NOTDONE)
		return "Received zero-length download but data is incomplete";
	if (status == FU_DFU_STATUS_ERR_FIRMWARE)
		return "Device firmware is corrupt";
	if (status == FU_DFU_STATUS_ERR_VENDOR)
		return "Vendor-specific error";
	if (status == FU_DFU_STATUS_ERR_USBR)
		return "Device detected unexpected USB reset signaling";
	if (status == FU_DFU_STATUS_ERR_POR)
		return "Device detected unexpected power on reset";
	if (status == FU_DFU_STATUS_ERR_UNKNOWN)
		return "Something unexpected went wrong";
	if (status == FU_DFU_STATUS_ERR_STALLDPKT)
		return "Device stalled an unexpected request";
	return NULL;
}

static gboolean
fu_dfu_target_manifest_wait(FuDfuTarget *self, GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint polling_count = 0;

	/* get the status */
	if (!fu_dfu_device_refresh(device, 0, error))
		return FALSE;

	/* wait for FU_DFU_STATE_DFU_MANIFEST to not be set */
	while (fu_dfu_device_get_state(device) == FU_DFU_STATE_DFU_MANIFEST_SYNC ||
	       fu_dfu_device_get_state(device) == FU_DFU_STATE_DFU_MANIFEST) {
		g_debug("waiting for FU_DFU_STATE_DFU_MANIFEST to clear");

		if (polling_count++ > DFU_TARGET_MANIFEST_MAX_POLLING_TRIES) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "reach to max polling tries");
			return FALSE;
		}

		fu_device_sleep(FU_DEVICE(device),
				fu_dfu_device_get_download_timeout(device) + 1000);
		if (!fu_dfu_device_refresh(device, 0, error))
			return FALSE;
	}

	/* in an error state */
	if (fu_dfu_device_get_state(device) == FU_DFU_STATE_DFU_ERROR) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    fu_dfu_target_status_to_error_msg(fu_dfu_device_get_status(device)));
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dfu_target_check_status(FuDfuTarget *self, GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	FuDfuStatus status;
	g_autoptr(GTimer) timer = g_timer_new();

	/* get the status */
	if (!fu_dfu_device_refresh(device, 0, error))
		return FALSE;

	/* wait for dfuDNBUSY to not be set */
	while (fu_dfu_device_get_state(device) == FU_DFU_STATE_DFU_DNBUSY) {
		g_debug("waiting for FU_DFU_STATE_DFU_DNBUSY to clear");
		fu_device_sleep(FU_DEVICE(device), fu_dfu_device_get_download_timeout(device));
		if (!fu_dfu_device_refresh(device, 0, error))
			return FALSE;
		/* this is a really long time to save fwupd in case
		 * the device has got wedged */
		if (g_timer_elapsed(timer, NULL) > 120.f) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "Stuck in DFU_DNBUSY");
			return FALSE;
		}
	}

	/* not in an error state */
	if (fu_dfu_device_get_state(device) != FU_DFU_STATE_DFU_ERROR)
		return TRUE;

	/* STM32-specific long errors */
	status = fu_dfu_device_get_status(device);
	if (fu_dfu_device_get_version(device) == FU_DFU_FIRMARE_VERSION_DFUSE) {
		if (status == FU_DFU_STATUS_ERR_VENDOR) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Read protection is active");
			return FALSE;
		}
		if (status == FU_DFU_STATUS_ERR_TARGET) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Address is wrong or unsupported");
			return FALSE;
		}
	}

	/* use a proper error description */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    fu_dfu_target_status_to_error_msg(status));
	return FALSE;
}

/**
 * fu_dfu_target_use_alt_setting:
 * @self: a #FuDfuTarget
 * @error: (nullable): optional return location for an error
 *
 * Opens a DFU-capable target.
 *
 * Returns: %TRUE for success
 **/
static gboolean
fu_dfu_target_use_alt_setting(FuDfuTarget *self, GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_DFU_TARGET(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* ensure interface is claimed */
	if (!fu_dfu_device_ensure_interface(device, error))
		return FALSE;

	/* use the correct setting */
	if (fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!g_usb_device_set_interface_alt(usb_device,
						    (gint)fu_dfu_device_get_interface(device),
						    (gint)priv->alt_setting,
						    &error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot set alternate setting 0x%02x on interface %i: %s",
				    priv->alt_setting,
				    fu_dfu_device_get_interface(device),
				    error_local->message);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * fu_dfu_target_setup:
 * @self: a #FuDfuTarget
 * @error: (nullable): optional return location for an error
 *
 * Opens a DFU-capable target.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_dfu_target_setup(FuDfuTarget *self, GError **error)
{
	FuDfuTargetClass *klass = FU_DFU_TARGET_GET_CLASS(self);
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	FuDevice *device = fu_device_get_proxy(FU_DEVICE(self));

	g_return_val_if_fail(FU_IS_DFU_TARGET(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->done_setup)
		return TRUE;

	/* superclassed */
	if (klass->setup != NULL) {
		if (!klass->setup(self, error))
			return FALSE;
	}

	/* GD32VF103 devices features and peripheral list */
	if (priv->alt_setting == 0x0 &&
	    fu_device_has_private_flag(device, FU_DFU_DEVICE_FLAG_GD32)) {
		/*             RB R8 R6 R4  VB V8
		 * Flash (KB) 128 64 32 16 128 64
		 *             TB T8 T6 T4  CB C8 C6 C4
		 * Flash (KB) 128 64 32 16 128 64 32 16
		 */
		const gchar *serial = fu_device_get_serial(device);
		if (serial == NULL || strlen(serial) < 4 || serial[3] != 'J') {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "GD32 serial number %s invalid",
				    serial);
			return FALSE;
		}
		if (serial[2] == '2') {
			fu_device_set_logical_id(FU_DEVICE(self),
						 "@Internal Flash  /0x8000000/8*1Kg");
		} else if (serial[2] == '4') {
			fu_device_set_logical_id(FU_DEVICE(self),
						 "@Internal Flash  /0x8000000/16*1Kg");
		} else if (serial[2] == '6') {
			fu_device_set_logical_id(FU_DEVICE(self),
						 "@Internal Flash  /0x8000000/32*1Kg");
		} else if (serial[2] == '8') {
			fu_device_set_logical_id(FU_DEVICE(self),
						 "@Internal Flash  /0x8000000/64*1Kg");
		} else if (serial[2] == 'B') {
			fu_device_set_logical_id(FU_DEVICE(self),
						 "@Internal Flash  /0x8000000/128*1Kg");
		} else if (serial[2] == 'D') {
			fu_device_set_logical_id(FU_DEVICE(self),
						 "@Internal Flash  /0x8000000/256*1Kg");
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Unknown GD32 sector size: %c",
				    serial[2]);
			return FALSE;
		}
	}

	/* get string */
	if (priv->alt_idx != 0x00 && fu_device_get_logical_id(FU_DEVICE(self)) == NULL) {
		GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
		g_autofree gchar *alt_name = NULL;
		alt_name = g_usb_device_get_string_descriptor(usb_device, priv->alt_idx, NULL);
		fu_device_set_logical_id(FU_DEVICE(self), alt_name);
	}

	/* parse the DfuSe format according to UM0424 */
	if (priv->sectors->len == 0) {
		if (!fu_dfu_target_parse_sectors(self,
						 fu_device_get_logical_id(FU_DEVICE(self)),
						 error))
			return FALSE;
	}

	/* add a dummy entry */
	if (priv->sectors->len == 0) {
		FuDfuSector *sector;
		sector =
		    fu_dfu_sector_new(0x0, /* addr */
				      0x0, /* size */
				      0x0, /* size_left */
				      0x0, /* zone */
				      0x0, /* number */
				      FU_DFU_SECTOR_CAP_READABLE | FU_DFU_SECTOR_CAP_WRITEABLE);
		g_debug("no UM0424 sector description in %s",
			fu_device_get_logical_id(FU_DEVICE(self)));
		g_ptr_array_add(priv->sectors, sector);
	}

	priv->done_setup = TRUE;
	return TRUE;
}

gboolean
fu_dfu_target_download_chunk(FuDfuTarget *self,
			     guint16 index,
			     GByteArray *buf,
			     guint timeout_ms,
			     FuProgress *progress,
			     GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	g_autoptr(GError) error_local = NULL;
	gsize actual_length;

	/* fall back to default */
	if (timeout_ms == 0)
		timeout_ms = fu_dfu_device_get_timeout(device);

	/* low level packet debugging */
	fu_dump_raw(G_LOG_DOMAIN, "Message", buf->data, buf->len);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   FU_DFU_REQUEST_DNLOAD,
					   index,
					   fu_dfu_device_get_interface(device),
					   buf->data,
					   buf->len,
					   &actual_length,
					   timeout_ms,
					   NULL,
					   &error_local)) {
		/* refresh the error code */
		fu_dfu_device_error_fixup(device, &error_local);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot download data: %s",
			    error_local->message);
		return FALSE;
	}

	/* for STM32 devices, the action only occurs when we do GetStatus --
	 * and it can take a long time to complete! */
	if (fu_dfu_device_get_version(device) == FU_DFU_FIRMARE_VERSION_DFUSE) {
		if (!fu_dfu_device_refresh(device, 35000, error))
			return FALSE;
	}

	/* wait for the device to write contents to the EEPROM */
	if (buf->len == 0 && fu_dfu_device_get_download_timeout(device) > 0)
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
	if (fu_dfu_device_get_download_timeout(device) > 0) {
		g_debug("sleeping for %ums…", fu_dfu_device_get_download_timeout(device));
		fu_device_sleep(FU_DEVICE(device), fu_dfu_device_get_download_timeout(device));
	}

	/* find out if the write was successful, waiting for BUSY to clear */
	if (!fu_dfu_target_check_status(self, error)) {
		g_prefix_error(error, "cannot wait for busy: ");
		return FALSE;
	}

	g_assert_cmpint(actual_length, ==, buf->len);
	return TRUE;
}

GBytes *
fu_dfu_target_upload_chunk(FuDfuTarget *self,
			   guint16 index,
			   gsize buf_sz,
			   FuProgress *progress,
			   GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	g_autoptr(GError) error_local = NULL;
	guint8 *buf;
	gsize actual_length;

	/* unset */
	if (buf_sz == 0)
		buf_sz = (gsize)fu_dfu_device_get_transfer_size(device);

	buf = g_new0(guint8, buf_sz);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   FU_DFU_REQUEST_UPLOAD,
					   index,
					   fu_dfu_device_get_interface(device),
					   buf,
					   buf_sz,
					   &actual_length,
					   fu_dfu_device_get_timeout(device),
					   NULL,
					   &error_local)) {
		/* refresh the error code */
		fu_dfu_device_error_fixup(device, &error_local);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot upload data: %s",
			    error_local->message);
		return NULL;
	}

	/* low level packet debugging */
	fu_dump_raw(G_LOG_DOMAIN, "Message", buf, actual_length);

	return g_bytes_new_take(buf, actual_length);
}

void
fu_dfu_target_set_alt_idx(FuDfuTarget *self, guint8 alt_idx)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	priv->alt_idx = alt_idx;
}

void
fu_dfu_target_set_alt_setting(FuDfuTarget *self, guint8 alt_setting)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	priv->alt_setting = alt_setting;
}

gboolean
fu_dfu_target_attach(FuDfuTarget *self, FuProgress *progress, GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	FuDfuTargetClass *klass = FU_DFU_TARGET_GET_CLASS(self);

	/* ensure populated */
	if (!fu_dfu_target_setup(self, error))
		return FALSE;

	/* implemented as part of a superclass */
	if (klass->attach != NULL)
		return klass->attach(self, progress, error);

	/* normal DFU mode just needs a bus reset */
	return fu_dfu_device_reset(device, progress, error);
}

static FuChunk *
fu_dfu_target_upload_element_dfu(FuDfuTarget *self,
				 guint32 address,
				 gsize expected_size,
				 gsize maximum_size,
				 FuProgress *progress,
				 GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	GBytes *chunk_tmp;
	guint percentage_size = expected_size > 0 ? expected_size : maximum_size;
	gsize total_size = 0;
	guint16 transfer_size = fu_dfu_device_get_transfer_size(device);
	g_autoptr(GBytes) contents = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* update UI */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);

	/* get all the chunks from the hardware */
	chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	for (guint16 idx = 0; idx < G_MAXUINT16; idx++) {
		guint32 chunk_size;

		/* read chunk of data */
		chunk_tmp = fu_dfu_target_upload_chunk(self,
						       idx,
						       0, /* device transfer size */
						       progress,
						       error);
		if (chunk_tmp == NULL)
			return NULL;

		/* keep a sum of all the chunks */
		chunk_size = (guint32)g_bytes_get_size(chunk_tmp);
		total_size += chunk_size;

		/* add to array */
		g_debug("got #%04x chunk of size %" G_GUINT32_FORMAT, idx, chunk_size);
		g_ptr_array_add(chunks, chunk_tmp);

		/* update UI */
		if (chunk_size > 0)
			fu_progress_set_percentage_full(progress, total_size, percentage_size);

		/* detect short write as EOF */
		if (chunk_size < transfer_size)
			break;
	}

	/* check final size */
	if (expected_size > 0) {
		if (total_size != expected_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid size, got %" G_GSIZE_FORMAT ", "
				    "expected %" G_GSIZE_FORMAT,
				    total_size,
				    expected_size);
			return NULL;
		}
	}

	/* done */
	fu_progress_set_percentage(progress, 100);

	/* create new image */
	contents = fu_dfu_utils_bytes_join_array(chunks);
	return fu_chunk_bytes_new(contents);
}

static FuChunk *
fu_dfu_target_upload_element(FuDfuTarget *self,
			     guint32 address,
			     gsize expected_size,
			     gsize maximum_size,
			     FuProgress *progress,
			     GError **error)
{
	FuDfuTargetClass *klass = FU_DFU_TARGET_GET_CLASS(self);

	/* implemented as part of a superclass */
	if (klass->upload_element != NULL) {
		return klass
		    ->upload_element(self, address, expected_size, maximum_size, progress, error);
	}
	return fu_dfu_target_upload_element_dfu(self,
						address,
						expected_size,
						maximum_size,
						progress,
						error);
}

static guint32
fu_dfu_target_get_size_of_zone(FuDfuTarget *self, guint16 zone)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	guint32 len = 0;
	for (guint i = 0; i < priv->sectors->len; i++) {
		FuDfuSector *sector = g_ptr_array_index(priv->sectors, i);
		if (fu_dfu_sector_get_zone(sector) != zone)
			continue;
		len += fu_dfu_sector_get_size(sector);
	}
	return len;
}

/* private */
gboolean
fu_dfu_target_upload(FuDfuTarget *self,
		     FuFirmware *firmware,
		     FuProgress *progress,
		     FuDfuTargetTransferFlags flags,
		     GError **error)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	FuDfuSector *sector;
	guint16 zone_cur;
	guint32 zone_size = 0;
	guint32 zone_last = G_MAXUINT;
	g_autoptr(FuFirmware) image = NULL;

	g_return_val_if_fail(FU_IS_DFU_TARGET(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* ensure populated */
	if (!fu_dfu_target_setup(self, error))
		return FALSE;

	/* can the target do this? */
	if (!fu_device_has_private_flag(fu_device_get_proxy(FU_DEVICE(self)),
					FU_DFU_DEVICE_FLAG_CAN_UPLOAD)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "target cannot do uploading");
		return FALSE;
	}

	/* use correct alt */
	if (!fu_dfu_target_use_alt_setting(self, error))
		return FALSE;

	/* no open?! */
	if (priv->sectors->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no sectors defined for target");
		return FALSE;
	}

	/* create a new image */
	image = fu_firmware_new();
	fu_firmware_set_id(image, fu_device_get_logical_id(FU_DEVICE(self)));
	fu_firmware_set_idx(image, priv->alt_setting);

	/* get all the sectors for the device */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, priv->sectors->len);
	for (guint i = 0; i < priv->sectors->len; i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* only upload to the start of any zone:sector */
		sector = g_ptr_array_index(priv->sectors, i);
		zone_cur = fu_dfu_sector_get_zone(sector);
		if (zone_cur == zone_last)
			continue;

		/* get the size of the entire continuous zone */
		zone_size = fu_dfu_target_get_size_of_zone(self, zone_cur);
		zone_last = zone_cur;

		/* get the first element from the hardware */
		g_debug("starting upload from 0x%08x (0x%04x)",
			fu_dfu_sector_get_address(sector),
			zone_size);
		chk = fu_dfu_target_upload_element(self,
						   fu_dfu_sector_get_address(sector),
						   0,	      /* expected */
						   zone_size, /* maximum */
						   fu_progress_get_child(progress),
						   error);
		if (chk == NULL)
			return FALSE;

		/* this chunk was uploaded okay */
		fu_firmware_add_chunk(image, chk);
		fu_progress_step_done(progress);
	}

	/* success */
	fu_firmware_add_image(firmware, image);
	return TRUE;
}

static gchar *
_g_bytes_compare_verbose(GBytes *bytes1, GBytes *bytes2)
{
	const guint8 *data1;
	const guint8 *data2;
	gsize length1;
	gsize length2;

	data1 = g_bytes_get_data(bytes1, &length1);
	data2 = g_bytes_get_data(bytes2, &length2);

	/* not the same length */
	if (length1 != length2) {
		return g_strdup_printf("got %" G_GSIZE_FORMAT " bytes, "
				       "expected %" G_GSIZE_FORMAT,
				       length1,
				       length2);
	}

	/* return 00 01 02 03 */
	for (guint i = 0; i < length1; i++) {
		if (data1[i] != data2[i]) {
			return g_strdup_printf("got 0x%02x, expected 0x%02x @ 0x%04x",
					       data1[i],
					       data2[i],
					       i);
		}
	}
	return NULL;
}

static gboolean
fu_dfu_target_download_element_dfu(FuDfuTarget *self,
				   FuChunk *chk,
				   FuProgress *progress,
				   FuDfuTargetTransferFlags flags,
				   GError **error)
{
	FuDfuDevice *device = FU_DFU_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint32 nr_chunks;
	guint16 transfer_size = fu_dfu_device_get_transfer_size(device);
	g_autoptr(GBytes) bytes = NULL;

	/* round up as we have to transfer incomplete blocks */
	bytes = fu_chunk_get_bytes(chk);
	nr_chunks = (guint)ceil((gdouble)g_bytes_get_size(bytes) / (gdouble)transfer_size);
	if (nr_chunks == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "zero-length firmware");
		return FALSE;
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint32 i = 0; i < nr_chunks + 1; i++) {
		gsize length;
		guint32 offset;
		g_autoptr(GByteArray) buf = g_byte_array_new();

		/* calculate the offset into the chunk data */
		offset = i * transfer_size;

		/* we have to write one final zero-sized chunk for EOF */
		if (i < nr_chunks) {
			g_autoptr(GBytes) bytes_tmp = NULL;
			length = g_bytes_get_size(bytes) - offset;
			if (length > transfer_size)
				length = transfer_size;
			bytes_tmp = fu_bytes_new_offset(bytes, offset, length, error);
			if (bytes_tmp == NULL)
				return FALSE;
			fu_byte_array_append_bytes(buf, bytes_tmp);
		}
		g_debug("writing #%04x chunk of size 0x%x", i, buf->len);
		if (!fu_dfu_target_download_chunk(self, i, buf, 0, progress, error))
			return FALSE;

		/* update UI */
		fu_progress_set_percentage_full(progress, i + 1, nr_chunks + 1);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_target_download_element(FuDfuTarget *self,
			       FuChunk *chk,
			       FuProgress *progress,
			       FuDfuTargetTransferFlags flags,
			       GError **error)
{
	FuDevice *device = fu_device_get_proxy(FU_DEVICE(self));
	FuDfuTargetClass *klass = FU_DFU_TARGET_GET_CLASS(self);

	/* progress */
	if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY &&
	    fu_device_has_private_flag(device, FU_DFU_DEVICE_FLAG_CAN_UPLOAD)) {
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 4, NULL);
	} else {
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_set_steps(progress, 1);
	}

	/* implemented as part of a superclass */
	if (klass->download_element != NULL) {
		if (!klass->download_element(self,
					     chk,
					     fu_progress_get_child(progress),
					     flags,
					     error))
			return FALSE;
	} else {
		if (!fu_dfu_target_download_element_dfu(self,
							chk,
							fu_progress_get_child(progress),
							flags,
							error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify */
	if (flags & DFU_TARGET_TRANSFER_FLAG_VERIFY &&
	    fu_device_has_private_flag(device, FU_DFU_DEVICE_FLAG_CAN_UPLOAD)) {
		g_autoptr(GBytes) bytes = NULL;
		g_autoptr(GBytes) bytes_tmp = NULL;
		g_autoptr(FuChunk) chunk_tmp = NULL;
		bytes = fu_chunk_get_bytes(chk);
		chunk_tmp = fu_dfu_target_upload_element(self,
							 fu_chunk_get_address(chk),
							 g_bytes_get_size(bytes),
							 g_bytes_get_size(bytes),
							 fu_progress_get_child(progress),
							 error);
		if (chunk_tmp == NULL)
			return FALSE;
		bytes_tmp = fu_chunk_get_bytes(chunk_tmp);
		if (g_bytes_compare(bytes_tmp, bytes) != 0) {
			g_autofree gchar *bytes_cmp_str = NULL;
			bytes_cmp_str = _g_bytes_compare_verbose(bytes_tmp, bytes);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "verify failed: %s",
				    bytes_cmp_str);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

/**
 * fu_dfu_target_download:
 * @self: a #FuDfuTarget
 * @image: a #FuFirmware
 * @flags: DFU target flags, e.g. %DFU_TARGET_TRANSFER_FLAG_VERIFY
 * @error: (nullable): optional return location for an error
 *
 * Downloads firmware from the host to the target, optionally verifying
 * the transfer.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_dfu_target_download(FuDfuTarget *self,
		       FuFirmware *image,
		       FuProgress *progress,
		       FuDfuTargetTransferFlags flags,
		       GError **error)
{
	FuDevice *device = fu_device_get_proxy(FU_DEVICE(self));
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) chunks = NULL;

	g_return_val_if_fail(FU_IS_DFU_TARGET(self), FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(image), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* ensure populated */
	if (!fu_dfu_target_setup(self, error))
		return FALSE;

	/* can the target do this? */
	if (!fu_device_has_private_flag(device, FU_DFU_DEVICE_FLAG_CAN_DOWNLOAD)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "target cannot do downloading");
		return FALSE;
	}

	/* use correct alt */
	if (!fu_dfu_target_use_alt_setting(self, error))
		return FALSE;

	/* download all chunks in the image to the device */
	chunks = fu_firmware_get_chunks(image, error);
	if (chunks == NULL)
		return FALSE;
	if (chunks->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no image chunks");
		return FALSE;
	}
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_debug("downloading chunk at 0x%04x", fu_chunk_get_address(chk));

		/* auto-detect missing firmware address -- this assumes
		 * that the first target is the main program memory and that
		 * there is only one element in the firmware file */
		if (flags & DFU_TARGET_TRANSFER_FLAG_ADDR_HEURISTIC &&
		    fu_chunk_get_address(chk) == 0x0 && chunks->len == 1 &&
		    priv->sectors->len > 0) {
			FuDfuSector *sector = g_ptr_array_index(priv->sectors, 0);
			g_debug("fixing up firmware address from 0x0 to 0x%x",
				fu_dfu_sector_get_address(sector));
			fu_chunk_set_address(chk, fu_dfu_sector_get_address(sector));
		}

		/* download to device */
		if (!fu_dfu_target_download_element(self,
						    chk,
						    fu_progress_get_child(progress),
						    flags,
						    error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	if (fu_device_has_private_flag(device, FU_DFU_DEVICE_FLAG_MANIFEST_POLL) &&
	    fu_device_has_private_flag(device, FU_DFU_DEVICE_FLAG_MANIFEST_TOL))
		if (!fu_dfu_target_manifest_wait(self, error))
			return FALSE;

	/* success */
	return TRUE;
}

/**
 * fu_dfu_target_get_alt_setting:
 * @self: a #FuDfuTarget
 *
 * Gets the alternate setting to use for this interface.
 *
 * Returns: the alternative setting, typically zero
 **/
guint8
fu_dfu_target_get_alt_setting(FuDfuTarget *self)
{
	FuDfuTargetPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_TARGET(self), 0xff);
	return priv->alt_setting;
}

static void
fu_dfu_target_class_init(FuDfuTargetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_dfu_target_to_string;
	object_class->finalize = fu_dfu_target_finalize;
}
