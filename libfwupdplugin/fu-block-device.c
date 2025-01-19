/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBlockDevice"

#include "config.h"

#ifdef HAVE_SCSI_SG_H
#include <stddef.h>
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
		g_autofree gchar *devpath = fu_udev_device_get_devpath(FU_UDEV_DEVICE(usb_device));

		/* copy the VID and PID, and reconstruct compatible IDs */
		if (!fu_device_probe(usb_device, error))
			return FALSE;
		fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(usb_device));
		fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(usb_device));
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
		if (devpath != NULL) {
			g_autofree gchar *physical_id = g_strdup_printf("DEVPATH=%s", devpath);
			fu_device_set_physical_id(device, physical_id);
		}
	}

	/* success */
	return TRUE;
}

#ifdef HAVE_SCSI_SG_H
static gboolean
fu_block_device_ioctl_buf_cb(FuIoctl *self, gpointer ptr, guint8 *buf, gsize bufsz, GError **error)
{
	struct sg_io_hdr *io_hdr = (struct sg_io_hdr *)ptr;
	io_hdr->dxferp = buf;
	io_hdr->dxfer_len = bufsz;
	return TRUE;
}

static gboolean
fu_block_device_ioctl_cdb_cb(FuIoctl *self, gpointer ptr, guint8 *buf, gsize bufsz, GError **error)
{
	struct sg_io_hdr *io_hdr = (struct sg_io_hdr *)ptr;
	io_hdr->cmdp = buf;
	io_hdr->cmd_len = bufsz;
	return TRUE;
}

static gboolean
fu_block_device_ioctl_sense_cb(FuIoctl *self,
			       gpointer ptr,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	struct sg_io_hdr *io_hdr = (struct sg_io_hdr *)ptr;
	io_hdr->sbp = buf;
	io_hdr->mx_sb_len = bufsz;
	return TRUE;
}
#endif

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
	    .dxfer_direction = SG_DXFER_NONE,
	    .timeout = FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);

	/* include these when generating the emulation event */
	fu_ioctl_add_key_as_u16(ioctl, "Request", SG_IO);
	fu_ioctl_add_key_as_u8(ioctl, "DxferDirection", io_hdr.dxfer_direction);
	fu_ioctl_add_const_buffer(ioctl, "Cdb", cdb, cdbsz, fu_block_device_ioctl_cdb_cb);
	fu_ioctl_add_mutable_buffer(ioctl,
				    "Sense",
				    sense_buffer,
				    sizeof(sense_buffer),
				    fu_block_device_ioctl_sense_cb);
	if (!fu_ioctl_execute(ioctl,
			      SG_IO,
			      (guint8 *)&io_hdr,
			      sizeof(io_hdr),
			      &rc,
			      5 * FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
			      FU_IOCTL_FLAG_RETRY,
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
	    .dxfer_direction = SG_DXFER_FROM_DEV,
	    .timeout = FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);

	/* include these when generating the emulation event */
	fu_ioctl_add_key_as_u16(ioctl, "Request", SG_IO);
	fu_ioctl_add_key_as_u8(ioctl, "DxferDirection", io_hdr.dxfer_direction);
	fu_ioctl_add_mutable_buffer(ioctl, NULL, buf, bufsz, fu_block_device_ioctl_buf_cb);
	fu_ioctl_add_const_buffer(ioctl, "Cdb", cdb, cdbsz, fu_block_device_ioctl_cdb_cb);
	fu_ioctl_add_mutable_buffer(ioctl,
				    "Sense",
				    sense_buffer,
				    sizeof(sense_buffer),
				    fu_block_device_ioctl_sense_cb);
	if (!fu_ioctl_execute(ioctl,
			      SG_IO,
			      (guint8 *)&io_hdr,
			      sizeof(io_hdr),
			      &rc,
			      5 * FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
			      FU_IOCTL_FLAG_RETRY,
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
	    .dxfer_direction = SG_DXFER_TO_DEV,
	    .timeout = FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);

	/* include these when generating the emulation event */
	fu_ioctl_add_key_as_u16(ioctl, "Request", SG_IO);
	fu_ioctl_add_key_as_u8(ioctl, "DxferDirection", io_hdr.dxfer_direction);
	fu_ioctl_add_const_buffer(ioctl, NULL, buf, bufsz, fu_block_device_ioctl_buf_cb);
	fu_ioctl_add_const_buffer(ioctl, "Cdb", cdb, cdbsz, fu_block_device_ioctl_cdb_cb);
	fu_ioctl_add_mutable_buffer(ioctl,
				    "Sense",
				    sense_buffer,
				    sizeof(sense_buffer),
				    fu_block_device_ioctl_sense_cb);
	if (!fu_ioctl_execute(ioctl,
			      SG_IO,
			      (guint8 *)&io_hdr,
			      sizeof(io_hdr),
			      &rc,
			      5 * FU_BLOCK_DEVICE_SG_IO_TIMEOUT,
			      FU_IOCTL_FLAG_RETRY,
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
