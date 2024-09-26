/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBlockDevice"

#include "config.h"

#ifdef HAVE_SCSI_SG_H
#include <scsi/sg.h>
#endif

#include "fu-block-device.h"
#include "fu-dump.h"
#include "fu-usb-device.h"

/**
 * FuBlockDevice
 *
 * See also: #FuUdevDevice
 */

G_DEFINE_TYPE(FuBlockDevice, fu_block_device, FU_TYPE_UDEV_DEVICE)

#define FU_BLOCK_DEVICE_SG_IO_SENSE_BUFFER_LEN 32    /* bytes */
#define FU_BLOCK_DEVICE_SG_IO_TIMEOUT	       20000 /* ms */

static gboolean
fu_block_device_probe(FuDevice *device, GError **error)
{
	g_autoptr(FuDevice) usb_device = NULL;

	/* block devices are weird in that the vendor and model are generic */
	usb_device = fu_device_get_backend_parent_with_subsystem(device, "usb:usb_device", NULL);
	if (usb_device != NULL) {
		g_autofree gchar *physical_id = NULL;

		/* copy the VID and PID, and reconstruct compatible IDs */
		if (!fu_device_probe(usb_device, error))
			return FALSE;
		fu_device_add_instance_str(device,
					   "VEN",
					   fu_device_get_instance_str(usb_device, "VID"));
		fu_device_add_instance_str(device,
					   "DEV",
					   fu_device_get_instance_str(usb_device, "PID"));
		if (!fu_device_build_instance_id_full(device,
						      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						      error,
						      "BLOCK",
						      "VEN",
						      NULL))
			return FALSE;
		if (!fu_device_build_instance_id(device, error, "BLOCK", "VEN", "DEV", NULL))
			return FALSE;
		fu_device_incorporate(device,
				      usb_device,
				      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
					  FU_DEVICE_INCORPORATE_FLAG_VID |
					  FU_DEVICE_INCORPORATE_FLAG_PID);

		/* USB devpath as physical ID */
		physical_id =
		    g_strdup_printf("DEVPATH=%s",
				    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(usb_device)));
		fu_device_set_physical_id(device, physical_id);
	}

	/* success */
	return TRUE;
}

/**
 * fu_block_device_sg_io_cmd_none:
 * @self: a #FuBlockDevice
 * @cdb: a cdb command
 * @cdbsz: sizeof @cdb
 * @error: (nullable): optional return location for an error
 *
 * Performs a SCSI IO command with no parameters.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_block_device_sg_io_cmd_none(FuBlockDevice *self, const guint8 *cdb, guint8 cdbsz, GError **error)
{
#ifdef HAVE_SCSI_SG_H
	guint8 sense_buffer[FU_BLOCK_DEVICE_SG_IO_SENSE_BUFFER_LEN] = {0};
	struct sg_io_hdr io_hdr = {
	    .interface_id = 'S',
	    .cmd_len = cdbsz,
	    .mx_sb_len = sizeof(sense_buffer),
	    .dxfer_direction = SG_DXFER_NONE,
	    .cmdp = (guint8 *)cdb,
	    .sbp = sense_buffer,
	    .timeout = FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  sizeof(io_hdr),
				  &rc,
				  5 * FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;
	if (io_hdr.status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command fail with status %x, senseKey 0x%02x, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    sense_buffer[2],
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported as scsi/sg.h not found");
	return FALSE;
#endif
}

/**
 * fu_block_device_sg_io_cmd_read:
 * @self: a #FuBlockDevice
 * @cdb: a cdb command
 * @cdbsz: sizeof @cdb
 * @buf: buffer to read into
 * @bufsz: sizeof @buf
 * @error: (nullable): optional return location for an error
 *
 * Performs a SCSI IO read command.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_block_device_sg_io_cmd_read(FuBlockDevice *self,
			       const guint8 *cdb,
			       gsize cdbsz,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
#ifdef HAVE_SCSI_SG_H
	guint8 sense_buffer[FU_BLOCK_DEVICE_SG_IO_SENSE_BUFFER_LEN] = {0};
	struct sg_io_hdr io_hdr = {
	    .interface_id = 'S',
	    .cmd_len = cdbsz,
	    .mx_sb_len = sizeof(sense_buffer),
	    .dxfer_direction = SG_DXFER_FROM_DEV,
	    .dxfer_len = bufsz,
	    .dxferp = buf,
	    .cmdp = (guint8 *)cdb,
	    .sbp = sense_buffer,
	    .timeout = FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  sizeof(io_hdr),
				  &rc,
				  5 * FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;
	if (io_hdr.status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command fail with status %x, senseKey 0x%02x, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    sense_buffer[2],
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

	if (bufsz > 0)
		fu_dump_raw(G_LOG_DOMAIN, "cmd data", buf, bufsz);

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported as scsi/sg.h not found");
	return FALSE;
#endif
}

/**
 * fu_block_device_sg_io_cmd_write:
 * @self: a #FuBlockDevice
 * @cdb: a cdb command
 * @cdbsz: sizeof @cdb
 * @buf: buffer to read from
 * @bufsz: sizeof @buf
 * @error: (nullable): optional return location for an error
 *
 * Performs a SCSI IO write command.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_block_device_sg_io_cmd_write(FuBlockDevice *self,
				const guint8 *cdb,
				gsize cdbsz,
				const guint8 *buf,
				gsize bufsz,
				GError **error)
{
#ifdef HAVE_SCSI_SG_H
	guint8 sense_buffer[FU_BLOCK_DEVICE_SG_IO_SENSE_BUFFER_LEN] = {0};
	struct sg_io_hdr io_hdr = {
	    .interface_id = 'S',
	    .cmd_len = cdbsz,
	    .mx_sb_len = sizeof(sense_buffer),
	    .dxfer_direction = SG_DXFER_TO_DEV,
	    .dxfer_len = bufsz,
	    .dxferp = (guint8 *)buf,
	    .cmdp = (guint8 *)cdb,
	    .sbp = sense_buffer,
	    .timeout = FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  sizeof(io_hdr),
				  &rc,
				  5 * FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;
	if (io_hdr.status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command fail with status %x, senseKey 0x%02x, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    sense_buffer[2],
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

	if (bufsz > 0)
		fu_dump_raw(G_LOG_DOMAIN, "cmd data", buf, bufsz);

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported as scsi/sg.h not found");
	return FALSE;
#endif
}

static void
fu_block_device_init(FuBlockDevice *self)
{
}

static void
fu_block_device_class_init(FuBlockDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_block_device_probe;
}
