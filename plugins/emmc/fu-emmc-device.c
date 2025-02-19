/*
 * Copyright 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/mmc/ioctl.h>
#include <sys/ioctl.h>

#include "fu-emmc-device.h"

/* From kernel linux/major.h */
#define MMC_BLOCK_MAJOR 179

/* From kernel linux/mmc/mmc.h */
#define MMC_SWITCH		   6	/* ac	[31:0] See below	R1b */
#define MMC_SEND_EXT_CSD	   8	/* adtc				R1  */
#define MMC_SWITCH_MODE_WRITE_BYTE 0x03 /* Set target to value */
#define MMC_WRITE_BLOCK		   24	/* adtc [31:0] data addr	R1  */
#define MMC_SET_BLOCK_COUNT	   23	/* adtc [31:0] data addr	R1  */
#define MMC_WRITE_MULTIPLE_BLOCK   25	/* adtc [31:0] data addr	R1  */

/* From kernel linux/mmc/core.h */
#define MMC_RSP_PRESENT	 (1 << 0)
#define MMC_RSP_CRC	 (1 << 2) /* expect valid crc */
#define MMC_RSP_BUSY	 (1 << 3) /* card may send busy */
#define MMC_RSP_OPCODE	 (1 << 4) /* response contains opcode */
#define MMC_RSP_SPI_S1	 (1 << 7) /* one status byte */
#define MMC_CMD_AC	 (0 << 5)
#define MMC_CMD_ADTC	 (1 << 5)
#define MMC_RSP_SPI_BUSY (1 << 10) /* card may send busy */
#define MMC_RSP_SPI_R1	 (MMC_RSP_SPI_S1)
#define MMC_RSP_SPI_R1B	 (MMC_RSP_SPI_S1 | MMC_RSP_SPI_BUSY)
#define MMC_RSP_R1	 (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_RSP_R1B	 (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE | MMC_RSP_BUSY)

/* EXT_CSD fields */
#define EXT_CSD_SUPPORTED_MODES	     493 /* RO */
#define EXT_CSD_FFU_FEATURES	     492 /* RO */
#define EXT_CSD_FFU_ARG_3	     490 /* RO */
#define EXT_CSD_FFU_ARG_2	     489 /* RO */
#define EXT_CSD_FFU_ARG_1	     488 /* RO */
#define EXT_CSD_FFU_ARG_0	     487 /* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_3 305 /* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_2 304 /* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_1 303 /* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_0 302 /* RO */
#define EXT_CSD_REV		     192
#define EXT_CSD_FW_CONFIG	     169 /* R/W */
#define EXT_CSD_DATA_SECTOR_SIZE     61	 /* R */
#define EXT_CSD_MODE_CONFIG	     30
#define EXT_CSD_MODE_OPERATION_CODES 29 /* W */
#define EXT_CSD_FFU_STATUS	     26 /* R */
#define EXT_CSD_REV_V5_1	     8
#define EXT_CSD_REV_V5_0	     7

/* EXT_CSD field definitions */
#define EXT_CSD_NORMAL_MODE    (0x00)
#define EXT_CSD_FFU_MODE       (0x01)
#define EXT_CSD_FFU_INSTALL    (0x01)
#define EXT_CSD_FFU	       (1 << 0)
#define EXT_CSD_UPDATE_DISABLE (1 << 0)
#define EXT_CSD_CMD_SET_NORMAL (1 << 0)

#define FU_EMMC_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

struct _FuEmmcDevice {
	FuUdevDevice parent_instance;
	guint32 sect_size;
	guint32 write_block_size;
};

G_DEFINE_TYPE(FuEmmcDevice, fu_emmc_device, FU_TYPE_UDEV_DEVICE)

static void
fu_emmc_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE(device);
	fwupd_codec_string_append_int(str, idt, "SectorSize", self->sect_size);
}

static gboolean
fu_emmc_device_get_sysattr_guint64(FuUdevDevice *device,
				   const gchar *name,
				   guint64 *val_out,
				   GError **error)
{
	g_autofree gchar *value = NULL;

	value = fu_udev_device_read_sysfs(device,
					  name,
					  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					  error);
	if (value == NULL)
		return FALSE;
	return fu_strtoull(value, val_out, 0, G_MAXUINT64, FU_INTEGER_BASE_16, error);
}

static gboolean
fu_emmc_device_probe(FuDevice *device, GError **error)
{
	guint64 flag;
	guint64 oemid = 0;
	guint64 manfid = 0;
	g_autofree gchar *attr_fwrev = NULL;
	g_autofree gchar *attr_manfid = NULL;
	g_autofree gchar *attr_name = NULL;
	g_autofree gchar *dev_basename = NULL;
	g_autofree gchar *man_oem_name = NULL;
	g_autoptr(FuUdevDevice) udev_parent = NULL;

	udev_parent =
	    FU_UDEV_DEVICE(fu_device_get_backend_parent_with_subsystem(device, "mmc:block", NULL));
	if (udev_parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no MMC parent");
		return FALSE;
	}

	/* ignore *rpmb and *boot* mmc block devices */
	if (fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device has no device-file");
		return FALSE;
	}
	dev_basename = g_path_get_basename(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)));
	if (!g_regex_match_simple("mmcblk\\d$",
				  dev_basename,
				  0,	/* G_REGEX_DEFAULT */
				  0)) { /* G_REGEX_MATCH_DEFAULT */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not raw mmc block device, devname=%s",
			    dev_basename);
		return FALSE;
	}

	/* doesn't support FFU */
	if (!fu_emmc_device_get_sysattr_guint64(udev_parent, "ffu_capable", &flag, error))
		return FALSE;
	if (flag == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s does not support field firmware updates",
			    fu_device_get_name(device));
		return FALSE;
	}

	/* name */
	attr_name = fu_udev_device_read_sysfs(udev_parent,
					      "name",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
	if (attr_name == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s does not have 'name' sysattr",
			    fu_device_get_name(device));
		return FALSE;
	}
	fu_device_add_instance_strsafe(device, "NAME", attr_name);
	fu_device_build_instance_id(device, NULL, "EMMC", "NAME", NULL);
	fu_device_set_name(device, attr_name);

	/* firmware version */
	attr_fwrev = fu_udev_device_read_sysfs(udev_parent,
					       "fwrev",
					       FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					       NULL);
	if (attr_fwrev != NULL) {
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);
		fu_device_set_version(device, attr_fwrev);
	}
	fu_device_add_instance_strsafe(device, "REV", attr_fwrev);
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV))
		fu_device_build_instance_id(device, NULL, "EMMC", "NAME", "REV", NULL);

	/* manfid + oemid, manfid + oemid + name */
	if (!fu_emmc_device_get_sysattr_guint64(udev_parent, "manfid", &manfid, error))
		return FALSE;
	if (!fu_emmc_device_get_sysattr_guint64(udev_parent, "oemid", &oemid, error))
		return FALSE;
	fu_device_add_instance_u16(device, "MAN", manfid);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "EMMC",
					 "MAN",
					 NULL);
	fu_device_add_instance_u16(device, "OEM", oemid);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "EMMC",
					 "MAN",
					 "OEM",
					 NULL);
	fu_device_build_instance_id(device, NULL, "EMMC", "MAN", "OEM", "NAME", NULL);
	fu_device_build_instance_id(device, NULL, "EMMC", "MAN", "NAME", "REV", NULL);
	fu_device_build_instance_id(device, NULL, "EMMC", "MAN", "OEM", "NAME", "REV", NULL);

	/* this is a (invalid!) instance ID added for legacy compatibility */
	man_oem_name = g_strdup_printf("EMMC\\%04" G_GUINT64_FORMAT "&%04" G_GUINT64_FORMAT "&%s",
				       manfid,
				       oemid,
				       fu_device_get_name(device));
	fu_device_add_instance_id(device, man_oem_name);

	/* set the vendor */
	attr_manfid = fu_udev_device_read_sysfs(udev_parent,
						"manfid",
						FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						error);
	if (attr_manfid == NULL)
		return FALSE;
	fu_device_build_vendor_id(device, "EMMC", attr_manfid);

	/* set the physical ID from the mmc parent */
	fu_device_set_physical_id(device, fu_device_get_backend_id(FU_DEVICE(udev_parent)));

	/* internal */
	if (!fu_emmc_device_get_sysattr_guint64(FU_UDEV_DEVICE(device), "removable", &flag, error))
		return FALSE;
	if (flag == 0)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);

	return TRUE;
}

static gboolean
fu_emmc_device_ioctl_buffer_cb(FuIoctl *self,
			       gpointer ptr,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	struct mmc_ioc_cmd *idata = (struct mmc_ioc_cmd *)ptr;
	idata->data_ptr = (guint64)((gulong)buf);
	return TRUE;
}

static gboolean
fu_emmc_device_read_extcsd(FuEmmcDevice *self, guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));
	struct mmc_ioc_cmd idata = {
	    .write_flag = 0,
	    .opcode = MMC_SEND_EXT_CSD,
	    .arg = 0,
	    .flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC,
	    .blksz = 512,
	    .blocks = 1,
	};

	/* include these when generating the emulation event */
	fu_ioctl_add_key_as_u16(ioctl, "Request", MMC_IOC_CMD);
	fu_ioctl_add_key_as_u8(ioctl, "Opcode", idata.opcode);
	fu_ioctl_add_mutable_buffer(ioctl, NULL, buf, bufsz, fu_emmc_device_ioctl_buffer_cb);
	if (!fu_ioctl_execute(ioctl,
			      MMC_IOC_CMD,
			      (guint8 *)&idata,
			      sizeof(idata),
			      NULL,
			      FU_EMMC_DEVICE_IOCTL_TIMEOUT,
			      FU_IOCTL_FLAG_NONE,
			      error)) {
		g_prefix_error(error, "failed to MMC_IOC_CMD: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_emmc_device_validate_extcsd(FuEmmcDevice *self, GError **error)
{
	guint8 ext_csd[512] = {0x0};

	if (!fu_emmc_device_read_extcsd(self, ext_csd, sizeof(ext_csd), error))
		return FALSE;
	if (ext_csd[EXT_CSD_REV] < EXT_CSD_REV_V5_0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "FFU is only available on devices >= "
			    "MMC 5.0, not supported in %s",
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}
	if ((ext_csd[EXT_CSD_SUPPORTED_MODES] & EXT_CSD_FFU) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "FFU is not supported in %s",
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}
	if (ext_csd[EXT_CSD_FW_CONFIG] & EXT_CSD_UPDATE_DISABLE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware update was disabled in %s",
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}
	self->sect_size = (ext_csd[EXT_CSD_DATA_SECTOR_SIZE] == 0) ? 512 : 4096;

	return TRUE;
}

static gboolean
fu_emmc_device_setup(FuDevice *device, GError **error)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE(device);
	g_autoptr(GError) error_validate = NULL;

	if (!fu_emmc_device_validate_extcsd(self, &error_validate))
		g_debug("%s", error_validate->message);
	else
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE);

	return TRUE;
}

static FuFirmware *
fu_emmc_device_prepare_firmware(FuDevice *device,
				GInputStream *stream,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_firmware_new();

	/* check alignment */
	if (fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if ((fu_firmware_get_size(firmware) % self->sect_size) > 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware data size (%" G_GSIZE_FORMAT ") is not aligned",
			    fu_firmware_get_size(firmware));
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_emmc_device_write_firmware(FuDevice *device,
			      FuFirmware *firmware,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE(device);
	guint32 arg;
	guint32 sect_done = 0;
	guint32 sector_size;
	gboolean check_sect_done = FALSE;
	gsize multi_cmdsz;
	guint8 ext_csd[512];
	guint failure_cnt = 0;
	g_autofree struct mmc_ioc_multi_cmd *multi_cmd = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "ffu");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 45, NULL);

	if (!fu_emmc_device_read_extcsd(self, ext_csd, sizeof(ext_csd), error))
		return FALSE;

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	sector_size = self->write_block_size ?: self->sect_size;

	/*  mode operation codes are supported */
	check_sect_done = (ext_csd[EXT_CSD_FFU_FEATURES] & 1) > 0;

	/* set CMD ARG */
	arg = ext_csd[EXT_CSD_FFU_ARG_0] | ext_csd[EXT_CSD_FFU_ARG_1] << 8 |
	      ext_csd[EXT_CSD_FFU_ARG_2] << 16 | ext_csd[EXT_CSD_FFU_ARG_3] << 24;

	/* prepare multi_cmd to be sent */
	multi_cmdsz = sizeof(struct mmc_ioc_multi_cmd) + 4 * sizeof(struct mmc_ioc_cmd);
	multi_cmd = g_malloc0(multi_cmdsz);
	multi_cmd->num_of_cmds = 4;

	/* put device into ffu mode */
	multi_cmd->cmds[0].opcode = MMC_SWITCH;
	multi_cmd->cmds[0].arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) | (EXT_CSD_MODE_CONFIG << 16) |
				 (EXT_CSD_FFU_MODE << 8) | EXT_CSD_CMD_SET_NORMAL;
	multi_cmd->cmds[0].flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
	multi_cmd->cmds[0].write_flag = 1;

	/* send block count */
	multi_cmd->cmds[1].opcode = MMC_SET_BLOCK_COUNT;
	multi_cmd->cmds[1].arg = sector_size / 512;
	multi_cmd->cmds[1].flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;

	/* send image chunk */
	multi_cmd->cmds[2].opcode = MMC_WRITE_MULTIPLE_BLOCK;
	multi_cmd->cmds[2].blksz = 512;
	multi_cmd->cmds[2].blocks = sector_size / 512;
	multi_cmd->cmds[2].arg = arg;
	multi_cmd->cmds[2].flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
	multi_cmd->cmds[2].write_flag = 1;

	/* return device into normal mode */
	multi_cmd->cmds[3].opcode = MMC_SWITCH;
	multi_cmd->cmds[3].arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) | (EXT_CSD_MODE_CONFIG << 16) |
				 (EXT_CSD_NORMAL_MODE << 8) | EXT_CSD_CMD_SET_NORMAL;
	multi_cmd->cmds[3].flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
	multi_cmd->cmds[3].write_flag = 1;
	fu_progress_step_done(progress);

	/* build packets */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						sector_size,
						error);
	if (chunks == NULL)
		return FALSE;
	while (failure_cnt < 3) {
		for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
			g_autoptr(FuChunk) chk = NULL;

			/* prepare chunk */
			chk = fu_chunk_array_index(chunks, i, error);
			if (chk == NULL)
				return FALSE;

			mmc_ioc_cmd_set_data(multi_cmd->cmds[2], fu_chunk_get_data(chk));

			if (!fu_ioctl_execute(ioctl,
					      MMC_IOC_MULTI_CMD,
					      (guint8 *)multi_cmd,
					      multi_cmdsz,
					      NULL,
					      FU_EMMC_DEVICE_IOCTL_TIMEOUT,
					      FU_IOCTL_FLAG_NONE,
					      error)) {
				g_autoptr(GError) error_local = NULL;
				g_prefix_error(error, "multi-cmd failed: ");
				/* multi-cmd ioctl failed before exiting from ffu mode */
				if (!fu_ioctl_execute(ioctl,
						      MMC_IOC_CMD,
						      (guint8 *)&multi_cmd->cmds[3],
						      sizeof(struct mmc_ioc_cmd),
						      NULL,
						      FU_EMMC_DEVICE_IOCTL_TIMEOUT,
						      FU_IOCTL_FLAG_NONE,
						      &error_local)) {
					g_prefix_error(error, "%s: ", error_local->message);
				}
				return FALSE;
			}

			/* update progress */
			fu_progress_set_percentage_full(fu_progress_get_child(progress),
							(gsize)i + 1,
							(gsize)fu_chunk_array_length(chunks));
		}

		if (!check_sect_done)
			break;

		if (!fu_emmc_device_read_extcsd(self, ext_csd, sizeof(ext_csd), error))
			return FALSE;

		sect_done = ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_0] |
			    ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_1] << 8 |
			    ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_2] << 16 |
			    ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_3] << 24;

		if (sect_done != 0)
			break;

		failure_cnt++;
		g_debug("programming failed: retrying (%u)", failure_cnt);
		fu_progress_step_done(progress);
	}

	fu_progress_step_done(progress);

	/* sanity check */
	if (check_sect_done) {
		gsize streamsz = 0;
		gsize total_done = (gsize)sect_done * (gsize)self->sect_size;
		if (!fu_input_stream_size(stream, &streamsz, error))
			return FALSE;
		if (total_done != streamsz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware size and number of sectors written "
				    "mismatch (%" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT "):",
				    total_done,
				    streamsz);
			return FALSE;
		}
	}

	/* check mode operation for ffu install*/
	if (!check_sect_done) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	} else {
		/* re-enter ffu mode and install the firmware */
		multi_cmd->num_of_cmds = 2;
		multi_cmdsz = sizeof(struct mmc_ioc_multi_cmd) +
			      multi_cmd->num_of_cmds * sizeof(struct mmc_ioc_cmd);

		/* set ext_csd to install mode */
		multi_cmd->cmds[1].opcode = MMC_SWITCH;
		multi_cmd->cmds[1].blksz = 0;
		multi_cmd->cmds[1].blocks = 0;
		multi_cmd->cmds[1].arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
					 (EXT_CSD_MODE_OPERATION_CODES << 16) |
					 (EXT_CSD_FFU_INSTALL << 8) | EXT_CSD_CMD_SET_NORMAL;
		multi_cmd->cmds[1].flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
		multi_cmd->cmds[1].write_flag = 1;

		/* send ioctl with multi-cmd */
		if (!fu_ioctl_execute(ioctl,
				      MMC_IOC_MULTI_CMD,
				      (guint8 *)multi_cmd,
				      multi_cmdsz,
				      NULL,
				      FU_EMMC_DEVICE_IOCTL_TIMEOUT,
				      FU_IOCTL_FLAG_NONE,
				      error)) {
			g_autoptr(GError) error_local = NULL;
			/* In case multi-cmd ioctl failed before exiting from ffu mode */
			g_prefix_error(error, "multi-cmd failed setting install mode: ");
			if (!fu_ioctl_execute(ioctl,
					      MMC_IOC_CMD,
					      (guint8 *)&multi_cmd->cmds[2],
					      sizeof(struct mmc_ioc_cmd),
					      NULL,
					      FU_EMMC_DEVICE_IOCTL_TIMEOUT,
					      FU_IOCTL_FLAG_NONE,
					      &error_local)) {
				g_prefix_error(error, "%s: ", error_local->message);
			}
			return FALSE;
		}

		/* return status */
		if (!fu_emmc_device_read_extcsd(self, ext_csd, sizeof(ext_csd), error))
			return FALSE;
		if (ext_csd[EXT_CSD_FFU_STATUS] != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "FFU install failed: %d",
				    ext_csd[EXT_CSD_FFU_STATUS]);
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	return TRUE;
}

static gboolean
fu_emmc_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE(device);
	if (g_strcmp0(key, "EmmcBlockSize") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->write_block_size = tmp;
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_emmc_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_emmc_device_init(FuEmmcDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "org.jedec.mmc");
	fu_device_add_icon(FU_DEVICE(self), "media-memory");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static void
fu_emmc_device_class_init(FuEmmcDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->set_quirk_kv = fu_emmc_device_set_quirk_kv;
	device_class->setup = fu_emmc_device_setup;
	device_class->to_string = fu_emmc_device_to_string;
	device_class->prepare_firmware = fu_emmc_device_prepare_firmware;
	device_class->probe = fu_emmc_device_probe;
	device_class->write_firmware = fu_emmc_device_write_firmware;
	device_class->set_progress = fu_emmc_device_set_progress;
}
