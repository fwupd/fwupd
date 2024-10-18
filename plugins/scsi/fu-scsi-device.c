/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <scsi/sg.h>

#include "fu-scsi-device.h"
#include "fu-scsi-struct.h"

struct _FuScsiDevice {
	FuUdevDevice parent_instance;
	guint64 ffu_timeout;
};

G_DEFINE_TYPE(FuScsiDevice, fu_scsi_device, FU_TYPE_UDEV_DEVICE)

#define INQUIRY_CMD	  0x12
#define INQUIRY_CMDLEN	  6
#define SCSI_INQ_BUFF_LEN 254

#define BUFFER_VENDOR_MODE 0x01
#define BUFFER_DUFS_MODE   0x02
#define BUFFER_FFU_MODE	   0x0E
#define BUFFER_EHS_MODE	   0x1C

#define SENSE_BUFF_LEN	 18
#define WRITE_BUF_CMDLEN 10
#define READ_BUF_CMDLEN	 10
#define WRITE_BUFFER_CMD 0x3B
#define READ_BUFFER_CMD	 0x3C

#define FU_SCSI_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static void
fu_scsi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuScsiDevice *self = FU_SCSI_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "FfuTimeout", self->ffu_timeout);
}

static gboolean
fu_scsi_device_probe(FuDevice *device, GError **error)
{
	FuScsiDevice *self = FU_SCSI_DEVICE(device);
	g_autofree gchar *attr_removable = NULL;
	g_autoptr(FuDevice) ufshci_parent = NULL;
	g_autoptr(FuDevice) device_target = NULL;
	g_autoptr(FuDevice) device_scsi = NULL;
	const gchar *subsystem_parents[] = {"pci", "platform", NULL};

	/* the ufshci controller could really be on any bus... search in order of priority */
	for (guint i = 0; subsystem_parents[i] != NULL && ufshci_parent == NULL; i++) {
		ufshci_parent =
		    fu_device_get_backend_parent_with_subsystem(device, subsystem_parents[i], NULL);
	}
	if (ufshci_parent != NULL) {
		g_autofree gchar *attr_ufs_features = NULL;
		g_autofree gchar *attr_ffu_timeout = NULL;

		/* check if this is a UFS device */
		g_info("found ufshci controller at %s",
		       fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(ufshci_parent)));

		attr_ufs_features =
		    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					      "device_descriptor/ufs_features",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
		if (attr_ufs_features != NULL) {
			guint64 ufs_features = 0;
			fu_device_set_summary(device, "UFS device");
			/* least significant bit specifies FFU capability */
			if (!fu_strtoull(attr_ufs_features,
					 &ufs_features,
					 0,
					 G_MAXUINT64,
					 FU_INTEGER_BASE_AUTO,
					 error))
				return FALSE;
			if (ufs_features & 0x1) {
				fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
				fu_device_add_private_flag(FU_DEVICE(self),
							   FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
				fu_device_add_protocol(device, "org.jedec.ufs");
			}
			attr_ffu_timeout =
			    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						      "device_descriptor/ffu_timeout",
						      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						      error);
			if (attr_ffu_timeout == NULL) {
				g_prefix_error(error, "no ffu timeout specified: ");
				return FALSE;
			}
			if (!fu_strtoull(attr_ffu_timeout,
					 &self->ffu_timeout,
					 0,
					 G_MAXUINT64,
					 FU_INTEGER_BASE_AUTO,
					 error))
				return FALSE;
		}
	}

	/* is internal? */
	attr_removable = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						   "removable",
						   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						   NULL);
	if (attr_removable != NULL) {
		guint64 removable = 0;
		if (!fu_strtoull(attr_removable,
				 &removable,
				 0,
				 G_MAXUINT64,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
		if (removable == 0x0)
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	}

	/* scsi_target */
	device_target =
	    fu_device_get_backend_parent_with_subsystem(device, "scsi:scsi_target", NULL);
	if (device_target != NULL) {
		g_autofree gchar *devpath =
		    fu_udev_device_get_devpath(FU_UDEV_DEVICE(device_target));
		if (devpath != NULL) {
			g_autofree gchar *physical_id = g_strdup_printf("DEVPATH=%s", devpath);
			fu_device_set_physical_id(device, physical_id);
		}
	}

	/* scsi_device */
	device_scsi = fu_device_get_backend_parent_with_subsystem(device, "scsi:scsi_device", NULL);
	if (device_scsi != NULL) {
		if (fu_device_get_vendor(device) == NULL) {
			g_autofree gchar *attr_vendor =
			    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device_scsi),
						      "vendor",
						      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						      NULL);
			if (attr_vendor != NULL)
				fu_device_set_vendor(device, attr_vendor);
		}
		if (fu_device_get_name(device) == NULL) {
			g_autofree gchar *attr_model =
			    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device_scsi),
						      "model",
						      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						      NULL);
			if (attr_model != NULL)
				fu_device_set_name(device, attr_model);
		}
	}

	/* fake something as we cannot use ioctls */
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_IS_FAKE)) {
		fu_device_add_instance_str(device, "VEN", "fwupd");
		fu_device_add_instance_str(device, "DEV", "DEVICE");
		if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", NULL))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_scsi_device_prepare_firmware(FuDevice *device,
				GInputStream *stream,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	fu_firmware_set_alignment(firmware, FU_FIRMWARE_ALIGNMENT_4K);
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_scsi_device_send_scsi_cmd_v3(FuScsiDevice *self,
				const guint8 *cdb,
				gsize cdbsz,
				const guint8 *buf,
				gsize bufsz,
				gint dir,
				GError **error)
{
	guint8 sense_buffer[SENSE_BUFF_LEN] = {0};
	struct sg_io_hdr io_hdr = {.interface_id = 'S'};

	io_hdr.cmd_len = cdbsz;
	io_hdr.mx_sb_len = sizeof(sense_buffer);
	io_hdr.dxfer_direction = dir;
	io_hdr.dxfer_len = bufsz;
	io_hdr.dxferp = (guint8 *)buf;
	/* pointer to command buf */
	io_hdr.cmdp = (guint8 *)cdb;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = 60000; /* ms */

	g_debug("cmd=0x%x len=0x%x", cdb[0], (guint)bufsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  sizeof(io_hdr),
				  NULL,
				  FU_SCSI_DEVICE_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;

	if (io_hdr.status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command fail with status %x, senseKey %s, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    fu_scsi_sense_key_to_string(sense_buffer[2]),
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_scsi_device_setup(FuDevice *device, GError **error)
{
	FuScsiDevice *self = FU_SCSI_DEVICE(device);
	guint8 buf[SCSI_INQ_BUFF_LEN] = {0};
	guint8 cdb[INQUIRY_CMDLEN] = {
	    INQUIRY_CMD,
	    0, /* evpd */
	    0, /* page */
	    0,
	    sizeof(buf),
	    0,
	};
	g_autofree gchar *model = NULL;
	g_autofree gchar *revision = NULL;
	g_autofree gchar *vendor = NULL;

	/* prepare chunk */
	if (!fu_scsi_device_send_scsi_cmd_v3(self,
					     cdb,
					     sizeof(cdb),
					     buf,
					     sizeof(buf),
					     SG_DXFER_FROM_DEV,
					     error)) {
		g_prefix_error(error, "SG_IO INQUIRY_CMD data error: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "INQUIRY", buf, sizeof(buf));

	/* parse */
	vendor = fu_strsafe((const gchar *)buf + 8, 8);
	if (vendor != NULL)
		fu_device_set_vendor(device, vendor);
	model = fu_strsafe((const gchar *)buf + 16, 8);
	if (model != NULL)
		fu_device_set_name(device, model);
	revision = fu_strsafe((const gchar *)buf + 32, 4);
	if (revision != NULL)
		fu_device_set_version(device, revision);

	/* add GUIDs */
	fu_device_add_instance_str(device, "VEN", vendor);
	fu_device_add_instance_str(device, "DEV", model);
	fu_device_add_instance_str(device, "REV", revision);
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "SCSI",
					      "VEN",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", "REV", NULL))
		return FALSE;

	/* vendor sanity */
	if (g_strcmp0(fu_device_get_vendor(device), "ATA") == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no assigned vendor");
		return FALSE;
	}
	fu_device_build_vendor_id(device, "SCSI", fu_device_get_vendor(device));

	/* success */
	return TRUE;
}

static gboolean
fu_scsi_device_write_firmware(FuDevice *device,
			      FuFirmware *firmware,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuScsiDevice *self = FU_SCSI_DEVICE(device);
	guint32 chunksz = 0x1000;
	guint32 offset = 0;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* prepare chunks */
	chunks = fu_chunk_array_new_from_stream(stream, 0x00, chunksz, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* write each block */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		guint8 cdb[WRITE_BUF_CMDLEN] = {WRITE_BUFFER_CMD,
						BUFFER_FFU_MODE,
						0x0 /* buf_id */};

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		fu_memwrite_uint24(cdb + 3, offset, G_BIG_ENDIAN);
		fu_memwrite_uint24(cdb + 6, fu_chunk_get_data_sz(chk), G_BIG_ENDIAN);
		if (!fu_scsi_device_send_scsi_cmd_v3(self,
						     cdb,
						     sizeof(cdb),
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     SG_DXFER_TO_DEV,
						     error)) {
			g_prefix_error(error,
				       "SG_IO WRITE BUFFER data error for v3 chunk 0x%x: ",
				       fu_chunk_get_idx(chk));

			return FALSE;
		}

		/* chunk done */
		fu_progress_step_done(progress);
		offset += fu_chunk_get_data_sz(chk);
	}

	/* success! */
	fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	return TRUE;
}

static void
fu_scsi_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_scsi_device_init(FuScsiDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), "drive-harddisk");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_summary(FU_DEVICE(self), "SCSI device");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_SYNC);
}

static void
fu_scsi_device_class_init(FuScsiDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_scsi_device_to_string;
	device_class->probe = fu_scsi_device_probe;
	device_class->setup = fu_scsi_device_setup;
	device_class->prepare_firmware = fu_scsi_device_prepare_firmware;
	device_class->write_firmware = fu_scsi_device_write_firmware;
	device_class->set_progress = fu_scsi_device_set_progress;
}
