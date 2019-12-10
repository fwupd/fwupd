/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include <sys/ioctl.h>
#include <linux/mmc/ioctl.h>

#include "fu-chunk.h"
#include "fu-emmc-device.h"

/* From kernel linux/major.h */
#define MMC_BLOCK_MAJOR			179

/* From kernel linux/mmc/mmc.h */
#define MMC_SWITCH			6	/* ac	[31:0] See below	R1b */
#define MMC_SEND_EXT_CSD		8	/* adtc				R1  */
#define MMC_SWITCH_MODE_WRITE_BYTE	0x03	/* Set target to value */
#define MMC_WRITE_BLOCK			24	/* adtc [31:0] data addr	R1  */

/* From kernel linux/mmc/core.h */
#define MMC_RSP_PRESENT	(1 << 0)
#define MMC_RSP_CRC	(1 << 2)		/* expect valid crc */
#define MMC_RSP_BUSY	(1 << 3)		/* card may send busy */
#define MMC_RSP_OPCODE	(1 << 4)		/* response contains opcode */
#define MMC_RSP_SPI_S1	(1 << 7)		/* one status byte */
#define MMC_CMD_AC	(0 << 5)
#define MMC_CMD_ADTC	(1 << 5)
#define MMC_RSP_SPI_BUSY (1 << 10)		/* card may send busy */
#define MMC_RSP_SPI_R1	(MMC_RSP_SPI_S1)
#define MMC_RSP_SPI_R1B	(MMC_RSP_SPI_S1|MMC_RSP_SPI_BUSY)
#define MMC_RSP_R1	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R1B	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE|MMC_RSP_BUSY)

/* EXT_CSD fields */
#define EXT_CSD_SUPPORTED_MODES		493	/* RO */
#define EXT_CSD_FFU_FEATURES		492	/* RO */
#define EXT_CSD_FFU_ARG_3		490	/* RO */
#define EXT_CSD_FFU_ARG_2		489	/* RO */
#define EXT_CSD_FFU_ARG_1		488	/* RO */
#define EXT_CSD_FFU_ARG_0		487	/* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_3	305	/* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_2	304	/* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_1	303	/* RO */
#define EXT_CSD_NUM_OF_FW_SEC_PROG_0	302	/* RO */
#define EXT_CSD_REV			192
#define EXT_CSD_FW_CONFIG		169	/* R/W */
#define EXT_CSD_DATA_SECTOR_SIZE	61	/* R */
#define EXT_CSD_MODE_CONFIG		30
#define EXT_CSD_MODE_OPERATION_CODES	29	/* W */
#define EXT_CSD_FFU_STATUS		26	/* R */
#define EXT_CSD_REV_V5_1		8
#define EXT_CSD_REV_V5_0		7

/* EXT_CSD field definitions */
#define EXT_CSD_NORMAL_MODE		(0x00)
#define EXT_CSD_FFU_MODE		(0x01)
#define EXT_CSD_FFU_INSTALL		(0x01)
#define EXT_CSD_FFU			(1<<0)
#define EXT_CSD_UPDATE_DISABLE		(1<<0)
#define EXT_CSD_CMD_SET_NORMAL		(1<<0)

struct _FuEmmcDevice {
	FuUdevDevice		 parent_instance;
	guint32			 sect_size;
};

G_DEFINE_TYPE (FuEmmcDevice, fu_emmc_device, FU_TYPE_UDEV_DEVICE)

static void
fu_emmc_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE (device);
	fu_common_string_append_ku (str, idt, "SectorSize", self->sect_size);
}

static const gchar *
fu_emmc_device_get_manufacturer (guint64 mmc_id)
{
	switch (mmc_id) {
	case 0x00:
	case 0x44:
		return "SanDisk";
	case 0x02:
		return "Kingston/Sandisk";
	case 0x03:
	case 0x11:
		return "Toshiba";
	case 0x13:
		return "Micron";
	case 0x15:
		return "Samsung/Sandisk/LG";
	case 0x37:
		return "Kingmax";
	case 0x70:
	case 0x2c:
		return "Kingston";
	default:
		return NULL;
	}
	return NULL;
}

static gboolean
fu_emmc_device_get_sysattr_guint64 (GUdevDevice *device,
				    const gchar *name,
				    guint64 *val_out,
				    GError **error)
{
	const gchar *sysfs;

	sysfs = g_udev_device_get_sysfs_attr (device, name);
	if (sysfs == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed get %s for %s", name, sysfs);
		return FALSE;
	}

	*val_out = g_ascii_strtoull (sysfs, NULL, 16);

	return TRUE;
}

static gboolean
fu_emmc_device_probe (FuUdevDevice *device, GError **error)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev (device);
	guint64 flag;
	guint64 oemid = 0;
	guint64 manfid = 0;
	const gchar *tmp;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autofree gchar *name_only = NULL;
	g_autofree gchar *man_oem = NULL;
	g_autofree gchar *man_oem_name = NULL;
	g_autofree gchar *vendor_id = NULL;

	udev_parent = g_udev_device_get_parent_with_subsystem (udev_device, "mmc", NULL);
	if (udev_parent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no MMC parent");
		return FALSE;
	}

	/* look for only the parent node */
	if (g_strcmp0 (g_udev_device_get_devtype (udev_device), "disk") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "is not correct devtype=%s, expected disk",
			     g_udev_device_get_devtype (udev_device));
		return FALSE;
	}

	/* doesn't support FFU */
	if (!fu_emmc_device_get_sysattr_guint64 (udev_parent, "ffu_capable", &flag, error))
		return FALSE;
	if (flag == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s does not support field firmware updates",
			     fu_device_get_name (FU_DEVICE (device)));
		return FALSE;
	}

	/* add instance IDs */
	tmp = g_udev_device_get_property (udev_device, "ID_NAME");
	if (tmp == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "has no ID_NAME");
		return FALSE;
	}

	/* name */
	fu_device_set_name (FU_DEVICE (device), tmp);
	name_only = g_strdup_printf ("EMMC\\%s", fu_device_get_name (device));
	fu_device_add_instance_id (FU_DEVICE (device), name_only);

	/* manfid + oemid, manfid + oemid + name */
	if (!fu_emmc_device_get_sysattr_guint64 (udev_parent, "manfid", &manfid, error))
		return FALSE;
	if (!fu_emmc_device_get_sysattr_guint64 (udev_parent, "oemid", &oemid, error))
		return FALSE;
	man_oem = g_strdup_printf ("EMMC\\%04" G_GUINT64_FORMAT "&%04" G_GUINT64_FORMAT,
				   manfid, oemid);
	fu_device_add_instance_id (FU_DEVICE (device), man_oem);
	man_oem_name = g_strdup_printf ("EMMC\\%04" G_GUINT64_FORMAT "&%04" G_GUINT64_FORMAT "&%s",
					manfid, oemid, fu_device_get_name (device));
	fu_device_add_instance_id (FU_DEVICE (device), man_oem_name);

	/* set the vendor */
	tmp = g_udev_device_get_sysfs_attr (udev_parent, "manfid");
	vendor_id = g_strdup_printf ("EMMC:%s", tmp);
	fu_device_set_vendor_id (FU_DEVICE (device), vendor_id);
	fu_device_set_vendor (FU_DEVICE (device), fu_emmc_device_get_manufacturer (manfid));

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (device, "mmc", error))
		return FALSE;

	/* internal */
	if (!fu_emmc_device_get_sysattr_guint64 (udev_device, "removable", &flag, error))
		return FALSE;
	if (flag == 0)
		fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_INTERNAL);

	/* firwmare version */
	tmp = g_udev_device_get_sysfs_attr (udev_parent, "fwrev");
	if (tmp != NULL)
		fu_device_set_version (FU_DEVICE (device), tmp, FWUPD_VERSION_FORMAT_NUMBER);

	return TRUE;
}

static gboolean
fu_emmc_read_extcsd (FuEmmcDevice *self, guint8 *ext_csd, gsize ext_csd_sz, GError **error)
{
	struct mmc_ioc_cmd idata = { 0x0 };
	idata.write_flag = 0;
	idata.opcode = MMC_SEND_EXT_CSD;
	idata.arg = 0;
	idata.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
	idata.blksz = 512;
	idata.blocks = 1;
	mmc_ioc_cmd_set_data (idata, ext_csd);
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				     MMC_IOC_CMD, (guint8 *) &idata,
				     NULL, error);
}

static gboolean
fu_emmc_validate_extcsd (FuDevice *device, GError **error)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE (device);
	guint8 ext_csd[512] = { 0x0 };

	if (!fu_emmc_read_extcsd (FU_EMMC_DEVICE (device), ext_csd, sizeof (ext_csd), error))
		return FALSE;
	if (ext_csd[EXT_CSD_REV] < EXT_CSD_REV_V5_0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "FFU is only available on devices >= "
			     "MMC 5.0, not supported in %s",
			     fu_device_get_name (device));
		return FALSE;
	}
	if ((ext_csd[EXT_CSD_SUPPORTED_MODES] & EXT_CSD_FFU) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "FFU is not supported in %s",
			     fu_device_get_name (device));
		return FALSE;
	}
	if (ext_csd[EXT_CSD_FW_CONFIG] & EXT_CSD_UPDATE_DISABLE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "firmware update was disabled in %s",
			     fu_device_get_name (device));
		return FALSE;
	}
	self->sect_size = (ext_csd[EXT_CSD_DATA_SECTOR_SIZE] == 0) ? 512 : 4096;

	return TRUE;
}

static gboolean
fu_emmc_device_setup (FuDevice *device, GError **error)
{
	g_autoptr(GError) error_validate = NULL;
	if (!fu_emmc_validate_extcsd (device, &error_validate))
		g_debug ("%s", error_validate->message);
	else
		fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);

	return TRUE;
}

static FuFirmware *
fu_emmc_device_prepare_firmware (FuDevice *device,
				 GBytes *fw,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuEmmcDevice *self = FU_EMMC_DEVICE (device);
	gsize fw_size = g_bytes_get_size (fw);

	/* check alignment */
	if ((fw_size % self->sect_size) > 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware data size (%" G_GSIZE_FORMAT ") is not aligned", fw_size);
		return NULL;
	}

	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_emmc_device_write_firmware (FuDevice *device,
			       FuFirmware *firmware,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuEmmcDevice *self= FU_EMMC_DEVICE (device);
	gsize fw_size = 0;
	gsize total_done;
	guint32 arg;
	guint32 sect_done = 0;
	guint8 ext_csd[512];
	guint failure_cnt = 0;
	g_autofree struct mmc_ioc_multi_cmd *multi_cmd = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	if (!fu_emmc_read_extcsd (FU_EMMC_DEVICE (device), ext_csd, sizeof (ext_csd), error))
		return FALSE;

	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	fw_size = g_bytes_get_size (fw);

	/* set CMD ARG */
	arg = ext_csd[EXT_CSD_FFU_ARG_0] |
	      ext_csd[EXT_CSD_FFU_ARG_1] << 8 |
	      ext_csd[EXT_CSD_FFU_ARG_2] << 16 |
	      ext_csd[EXT_CSD_FFU_ARG_3] << 24;

	/* prepare multi_cmd to be sent */
	multi_cmd = g_malloc0 (sizeof(struct mmc_ioc_multi_cmd) +
			       3 * sizeof(struct mmc_ioc_cmd));
	multi_cmd->num_of_cmds = 3;

	/* put device into ffu mode */
	multi_cmd->cmds[0].opcode = MMC_SWITCH;
	multi_cmd->cmds[0].arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
				 (EXT_CSD_MODE_CONFIG << 16) |
				 (EXT_CSD_FFU_MODE << 8) |
				  EXT_CSD_CMD_SET_NORMAL;
	multi_cmd->cmds[0].flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
	multi_cmd->cmds[0].write_flag = 1;

	/* send image chunk */
	multi_cmd->cmds[1].opcode = MMC_WRITE_BLOCK;
	multi_cmd->cmds[1].blksz = self->sect_size;
	multi_cmd->cmds[1].blocks = 1;
	multi_cmd->cmds[1].arg = arg;
	multi_cmd->cmds[1].flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
	multi_cmd->cmds[1].write_flag = 1;

	/* return device into normal mode */
	multi_cmd->cmds[2].opcode = MMC_SWITCH;
	multi_cmd->cmds[2].arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
				 (EXT_CSD_MODE_CONFIG << 16) |
				 (EXT_CSD_NORMAL_MODE << 8) |
				  EXT_CSD_CMD_SET_NORMAL;
	multi_cmd->cmds[2].flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
	multi_cmd->cmds[2].write_flag = 1;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						self->sect_size);
	while (sect_done == 0) {
		for (guint i = 0; i < chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index (chunks, i);

			mmc_ioc_cmd_set_data (multi_cmd->cmds[1], chk->data);

			if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
						   MMC_IOC_MULTI_CMD, (guint8 *) multi_cmd,
						   NULL, error)) {
				g_prefix_error (error, "multi-cmd failed: ");
				/* multi-cmd ioctl failed before exiting from ffu mode */
				fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
						      MMC_IOC_CMD, (guint8 *) &multi_cmd->cmds[2],
						      NULL, NULL);
				return FALSE;
			}

			if (!fu_emmc_read_extcsd (self, ext_csd, sizeof (ext_csd), error))
				return FALSE;

			/* if we need to restart the download */
			sect_done = ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_0] |
				    ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_1] << 8 |
				    ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_2] << 16 |
				    ext_csd[EXT_CSD_NUM_OF_FW_SEC_PROG_3] << 24;
			if (sect_done == 0) {
				if (failure_cnt >= 3) {
					g_set_error_literal (error,
							     G_IO_ERROR,
							     G_IO_ERROR_FAILED,
							     "programming failed");
					return FALSE;
				}
				failure_cnt++;
				g_debug ("programming failed: retrying (%u)", failure_cnt);
				break;
			}

			/* update progress */
			fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len - 1);
		}
	}

	/* sanity check */
	total_done = (gsize) sect_done * (gsize) self->sect_size;
	if (total_done != fw_size) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "firmware size and number of sectors written "
			     "mismatch (%" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT "):",
			     total_done, fw_size);
		return FALSE;
	}

	/* check mode operation for ffu install*/
	if (!ext_csd[EXT_CSD_FFU_FEATURES]) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	} else {
		/* re-enter ffu mode and install the firmware */
		multi_cmd->num_of_cmds = 2;

		/* set ext_csd to install mode */
		multi_cmd->cmds[1].opcode = MMC_SWITCH;
		multi_cmd->cmds[1].blksz = 0;
		multi_cmd->cmds[1].blocks = 0;
		multi_cmd->cmds[1].arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
					 (EXT_CSD_MODE_OPERATION_CODES << 16) |
					 (EXT_CSD_FFU_INSTALL << 8) |
					 EXT_CSD_CMD_SET_NORMAL;
		multi_cmd->cmds[1].flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
		multi_cmd->cmds[1].write_flag = 1;

		/* send ioctl with multi-cmd */
		if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
					   MMC_IOC_MULTI_CMD, (guint8 *) multi_cmd,
					   NULL, error)) {
			/* In case multi-cmd ioctl failed before exiting from ffu mode */
			g_prefix_error (error, "multi-cmd failed setting install mode: ");
			fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
					      MMC_IOC_CMD, (guint8 *) &multi_cmd->cmds[2],
					      NULL, NULL);
			return FALSE;
		}

		/* return status */
		if (!fu_emmc_read_extcsd (self, ext_csd, sizeof (ext_csd), error))
			return FALSE;
		if (ext_csd[EXT_CSD_FFU_STATUS] != 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "FFU install failed: %d",
				     ext_csd[EXT_CSD_FFU_STATUS]);
			return FALSE;
		}
	}

	return TRUE;
}

static void
fu_emmc_device_init (FuEmmcDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "org.jedec.mmc");
	fu_device_add_icon (FU_DEVICE (self), "media-memory");
}

static void
fu_emmc_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_emmc_device_parent_class)->finalize (object);
}

static void
fu_emmc_device_class_init (FuEmmcDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_emmc_device_finalize;
	klass_device->setup = fu_emmc_device_setup;
	klass_device->to_string = fu_emmc_device_to_string;
	klass_device->prepare_firmware = fu_emmc_device_prepare_firmware;
	klass_udev_device->probe = fu_emmc_device_probe;
	klass_device->write_firmware = fu_emmc_device_write_firmware;
}
