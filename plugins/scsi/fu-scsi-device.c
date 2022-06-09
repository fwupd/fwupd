/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <scsi/sg.h>

#include "fu-scsi-device.h"

struct _FuScsiDevice {
	FuUdevDevice parent_instance;
	guint64 ffu_timeout;
};

G_DEFINE_TYPE(FuScsiDevice, fu_scsi_device, FU_TYPE_UDEV_DEVICE)

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
	FU_DEVICE_CLASS(fu_scsi_device_parent_class)->to_string(device, idt, str);
	fu_string_append_kx(str, idt, "FfuTimeout", self->ffu_timeout);
}

static gboolean
fu_scsi_device_probe(FuDevice *device, GError **error)
{
	FuScsiDevice *self = FU_SCSI_DEVICE(device);
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	guint64 removable = 0;
	g_autofree gchar *vendor_id = NULL;
	g_autoptr(FuUdevDevice) ufshci_parent = NULL;
	const gchar *subsystem_parents[] = {"pci", "platform", NULL};

	/* check is valid */
	if (g_strcmp0(g_udev_device_get_devtype(udev_device), "disk") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct devtype=%s, expected disk",
			    g_udev_device_get_devtype(udev_device));
		return FALSE;
	}
	if (!g_udev_device_get_property_as_boolean(udev_device, "ID_SCSI")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "has no ID_SCSI");
		return FALSE;
	}

	/* vendor sanity */
	if (g_strcmp0(fu_device_get_vendor(device), "ATA") == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no assigned vendor");
		return FALSE;
	}

	vendor_id = g_strdup_printf("SCSI:%s", fu_device_get_vendor(device));
	fu_device_add_vendor_id(device, vendor_id);

	/* the ufshci controller could really be on any bus... search in order of priority */
	for (guint i = 0; subsystem_parents[i] != NULL && ufshci_parent == NULL; i++) {
		ufshci_parent = fu_udev_device_get_parent_with_subsystem(FU_UDEV_DEVICE(device),
									 subsystem_parents[i]);
	}
	if (ufshci_parent != NULL) {
		guint64 ufs_features = 0;

		/* check if this is a UFS device */
		g_debug("found ufshci controller at %s",
			fu_udev_device_get_sysfs_path(ufshci_parent));
		if (fu_udev_device_get_sysfs_attr_uint64(ufshci_parent,
							 "device_descriptor/ufs_features",
							 &ufs_features,
							 NULL)) {
			fu_device_set_summary(device, "UFS device");
			/* least significant bit specifies FFU capability */
			if (ufs_features & 0x1) {
				fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
				fu_device_add_internal_flag(FU_DEVICE(self),
							    FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
				fu_device_add_protocol(device, "org.jedec.ufs");
			}
			if (!fu_udev_device_get_sysfs_attr_uint64(ufshci_parent,
								  "device_descriptor/ffu_timeout",
								  &self->ffu_timeout,
								  error)) {
				g_prefix_error(error, "no ffu timeout specified: ");
				return FALSE;
			}
		}
	}

	/* add GUIDs */
	fu_device_add_instance_strsafe(device, "VEN", fu_device_get_vendor(device));
	fu_device_add_instance_strsafe(device, "DEV", fu_device_get_name(device));
	fu_device_add_instance_strsafe(device, "REV", fu_device_get_version(device));
	if (!fu_device_build_instance_id_quirk(device, error, "SCSI", "VEN", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", "REV", NULL))
		return FALSE;

	/* is internal? */
	if (fu_udev_device_get_sysfs_attr_uint64(FU_UDEV_DEVICE(device),
						 "removable",
						 &removable,
						 NULL)) {
		if (removable == 0x0)
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "scsi:scsi_target", error);
}

static FuFirmware *
fu_scsi_device_prepare_firmware(FuDevice *device,
				GBytes *fw,
				FwupdInstallFlags flags,
				GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	fu_firmware_set_alignment(firmware, FU_FIRMWARE_ALIGNMENT_4K);
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static const gchar *
fu_scsi_device_sense_key_to_string(guint8 key)
{
	if (key == 0x00)
		return "no-sense";
	if (key == 0x01)
		return "recovered-error";
	if (key == 0x02)
		return "not-ready";
	if (key == 0x03)
		return "medium-error";
	if (key == 0x04)
		return "hardware-error";
	if (key == 0x05)
		return "illegal-request";
	if (key == 0x06)
		return "unit-attention";
	if (key == 0x07)
		return "data-protect";
	if (key == 0x08)
		return "blank-check";
	if (key == 0x09)
		return "vendor-specific";
	if (key == 0x0A)
		return "copy-aborted";
	if (key == 0x0B)
		return "aborted-command";
	if (key == 0x0C)
		return "equal";
	if (key == 0x0D)
		return "volume-overflow";
	if (key == 0x0E)
		return "miscompare";
	return NULL;
}

static gboolean
fu_scsi_device_send_scsi_cmd_v3(FuScsiDevice *self,
				const guint8 *cdb,
				guint8 cdbsz,
				const guint8 *buf,
				guint32 bufsz,
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

	if (g_getenv("FWUPD_SCSI_VERBOSE") != NULL)
		g_debug("cmd=0x%x len=0x%x", cdb[0], (guint)bufsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  NULL,
				  FU_SCSI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;

	if (io_hdr.status) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Command fail with status %x, senseKey %s, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    fu_scsi_device_sense_key_to_string(sense_buffer[2]),
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

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
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* prepare chunks */
	chunks = fu_chunk_array_new_from_bytes(fw, 0x00, 0x00, chunksz);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, chunks->len);

	/* write each block */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 cdb[WRITE_BUF_CMDLEN] = {WRITE_BUFFER_CMD,
						BUFFER_FFU_MODE,
						0x0 /* buf_id */};

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
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_SYNC |
				     FU_UDEV_DEVICE_FLAG_IOCTL_RETRY);
}

static void
fu_scsi_device_class_init(FuScsiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_scsi_device_to_string;
	klass_device->probe = fu_scsi_device_probe;
	klass_device->prepare_firmware = fu_scsi_device_prepare_firmware;
	klass_device->write_firmware = fu_scsi_device_write_firmware;
	klass_device->set_progress = fu_scsi_device_set_progress;
}
