/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <scsi/sg.h>

#include "fu-ata-device.h"
#include "fu-chunk.h"

#define FU_ATA_IDENTIFY_SIZE	512	/* bytes */
#define FU_ATA_BLOCK_SIZE	512	/* bytes */

struct ata_tf {
	guint8			dev;
	guint8			command;
	guint8			error;
	guint8			status;
	guint8			feat;
	guint8			nsect;
	guint8			lbal;
	guint8			lbam;
	guint8			lbah;
};

#define ATA_USING_LBA			(1 << 6)
#define ATA_STAT_DRQ			(1 << 3)
#define ATA_STAT_ERR			(1 << 0)

#define ATA_OP_IDENTIFY			0xec
#define ATA_OP_FLUSH_CACHE		0xe7
#define ATA_OP_DOWNLOAD_MICROCODE	0x92
#define ATA_OP_STANDBY_IMMEDIATE	0xe0

#define ATA_SUBCMD_MICROCODE_OBSOLETE			0x01
#define ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS_ACTIVATE	0x03
#define ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNK		0x07
#define ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS		0x0e
#define ATA_SUBCMD_MICROCODE_ACTIVATE			0x0f

#define SG_CHECK_CONDITION		0x02
#define SG_DRIVER_SENSE			0x08

#define SG_ATA_12			0xa1
#define SG_ATA_12_LEN			12

#define SG_ATA_PROTO_NON_DATA		(3 << 1)
#define SG_ATA_PROTO_PIO_IN		(4 << 1)
#define SG_ATA_PROTO_PIO_OUT		(5 << 1)

enum {
	SG_CDB2_TLEN_NODATA	= 0 << 0,
	SG_CDB2_TLEN_FEAT	= 1 << 0,
	SG_CDB2_TLEN_NSECT	= 2 << 0,

	SG_CDB2_TLEN_BYTES	= 0 << 2,
	SG_CDB2_TLEN_SECTORS	= 1 << 2,

	SG_CDB2_TDIR_TO_DEV	= 0 << 3,
	SG_CDB2_TDIR_FROM_DEV	= 1 << 3,

	SG_CDB2_CHECK_COND	= 1 << 5,
};

struct _FuAtaDevice {
	FuUdevDevice		 parent_instance;
	guint			 pci_depth;
	guint			 usb_depth;
	guint16			 transfer_blocks;
	guint8			 transfer_mode;
};

G_DEFINE_TYPE (FuAtaDevice, fu_ata_device, FU_TYPE_UDEV_DEVICE)

guint8
fu_ata_device_get_transfer_mode (FuAtaDevice *self)
{
	return self->transfer_mode;
}

guint16
fu_ata_device_get_transfer_blocks (FuAtaDevice *self)
{
	return self->transfer_blocks;
}

static gchar *
fu_ata_device_get_string (const guint16 *buf, guint start, guint end)
{
	g_autoptr(GString) str = g_string_new (NULL);
	for (guint i = start; i <= end; i++) {
		g_string_append_c (str, (gchar) (buf[i] >> 8));
		g_string_append_c (str, (gchar) (buf[i] & 0xff));
	}

	/* remove whitespace before returning */
	if (str->len > 0) {
		g_strstrip (str->str);
		if (str->str[0] == '\0')
			return NULL;
	}
	return g_string_free (g_steal_pointer (&str), FALSE);
}

static void
fu_ata_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuAtaDevice *self = FU_ATA_DEVICE (device);
	fu_common_string_append_kx (str, idt, "TransferMode", self->transfer_mode);
	fu_common_string_append_kx (str, idt, "TransferBlocks", self->transfer_blocks);
	fu_common_string_append_ku (str, idt, "PciDepth", self->pci_depth);
	fu_common_string_append_ku (str, idt, "UsbDepth", self->usb_depth);
}

/* https://docs.microsoft.com/en-us/windows-hardware/drivers/install/identifiers-for-ide-devices */
static gchar *
fu_ata_device_pad_string_for_id (const gchar *name)
{
	GString *str = g_string_new (name);
	fu_common_string_replace (str, " ", "_");
	for (guint i = str->len; i < 40; i++)
		g_string_append_c (str, '_');
	return g_string_free (str, FALSE);
}

static gchar *
fu_ata_device_get_guid_safe (const guint16 *buf, guint16 addr_start)
{
	if (!fu_common_guid_is_plausible ((guint8 *) (buf + addr_start)))
		return NULL;
	return fwupd_guid_to_string ((const fwupd_guid_t *) (buf + addr_start),
				     FWUPD_GUID_FLAG_MIXED_ENDIAN);
}

static void
fu_ata_device_parse_id_maybe_dell (FuAtaDevice *self, const guint16 *buf)
{
	g_autofree gchar *component_id = NULL;
	g_autofree gchar *guid_efi = NULL;
	g_autofree gchar *guid_id = NULL;
	g_autofree gchar *guid = NULL;

	/* add extra component ID if set */
	component_id = fu_ata_device_get_string (buf, 137, 140);
	if (component_id == NULL ||
	   !g_str_is_ascii (component_id) ||
	    strlen (component_id) < 6) {
		g_debug ("invalid component ID, skipping");
		return;
	}

	/* do not add the FuUdevDevice instance IDs as generic firmware
	 * should not be used on these OEM-specific devices */
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS);

	/* add instance ID *and* GUID as using no-auto-instance-ids */
	guid_id = g_strdup_printf ("STORAGE-DELL-%s", component_id);
	fu_device_add_instance_id (FU_DEVICE (self), guid_id);
	guid = fwupd_guid_hash_string (guid_id);
	fu_device_add_guid (FU_DEVICE (self), guid);

	/* also add the EFI GUID */
	guid_efi = fu_ata_device_get_guid_safe (buf, 129);
	if (guid_efi != NULL)
		fu_device_add_guid (FU_DEVICE (self), guid_efi);
}

static gboolean
fu_ata_device_parse_id (FuAtaDevice *self, const guint8 *buf, gsize sz, GError **error)
{
	FuDevice *device = FU_DEVICE (self);
	guint16 xfer_min = 1;
	guint16 xfer_max = 0xffff;
	guint16 id[FU_ATA_IDENTIFY_SIZE/2];
	g_autofree gchar *name_pad = NULL;
	g_autofree gchar *sku = NULL;

	/* check size */
	if (sz != FU_ATA_IDENTIFY_SIZE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "ID incorrect size, got 0x%02x",
			     (guint) sz);
		return FALSE;
	}

	/* read LE buffer */
	for (guint i = 0; i < sz / 2; i++)
		id[i] = fu_common_read_uint16 (buf + (i * 2), G_LITTLE_ENDIAN);

	/* verify drive correctly supports DOWNLOAD_MICROCODE */
	if (!(id[83] & 1 && id[86] & 1)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "DOWNLOAD_MICROCODE not supported by device");
		return FALSE;
	}

	fu_ata_device_parse_id_maybe_dell (self, id);

	/* firmware will be applied when the device restarts */
	if (self->transfer_mode == ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS)
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);

	/* the newer, segmented transfer mode */
	if (self->transfer_mode == ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS_ACTIVATE ||
	    self->transfer_mode == ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS) {
		xfer_min = id[234];
		if (xfer_min == 0x0 || xfer_min == 0xffff)
			xfer_min = 1;
		xfer_max = id[235];
		if (xfer_max == 0x0 || xfer_max == 0xffff)
			xfer_max = xfer_min;
	}

	/* fall back to a sane block size */
	if (self->transfer_blocks == 0x0)
		self->transfer_blocks = xfer_min;
	else if (self->transfer_blocks == 0xffff)
		self->transfer_blocks = xfer_max;

	/* get values in case the kernel didn't */
	if (fu_device_get_serial (device) == NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = fu_ata_device_get_string (id, 10, 19);
		if (tmp != NULL)
			fu_device_set_serial (device, tmp);
	}
	if (fu_device_get_name (device) == NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = fu_ata_device_get_string (id, 27, 46);
		if (tmp != NULL)
			fu_device_set_name (device, tmp);
	}
	if (fu_device_get_version (device) == NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = fu_ata_device_get_string (id, 23, 26);
		if (tmp != NULL)
			fu_device_set_version (device, tmp, FWUPD_VERSION_FORMAT_PLAIN);
	} else {
		fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_PLAIN);
	}

	/* 8 byte additional product identifier == SKU? */
	sku = fu_ata_device_get_string (id, 170, 173);
	if (sku != NULL)
		g_debug ("SKU=%s", sku);

	/* if we have vendor defined identify blocks don't add generic GUID */
	if (fu_device_get_guids (device)->len != 0)
		return TRUE;

	/* add extra GUIDs if none detected from identify block */
	name_pad = fu_ata_device_pad_string_for_id (fu_device_get_name (device));
	if (name_pad != NULL &&
	    fu_device_get_version (device) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = g_strdup_printf ("IDE\\%s%s", name_pad,
				       fu_device_get_version (device));
		fu_device_add_instance_id (device, tmp);
	}
	if (name_pad != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = g_strdup_printf ("IDE\\0%s", name_pad);
		fu_device_add_instance_id (device, tmp);
	}

	/* add the name fallback */
	fu_device_add_instance_id (device, fu_device_get_name (device));

	return TRUE;
}

static gboolean
fu_ata_device_probe (FuUdevDevice *device, GError **error)
{
	FuAtaDevice *self = FU_ATA_DEVICE (device);
	GUdevDevice *udev_device = fu_udev_device_get_dev (device);

	/* check is valid */
	if (g_strcmp0 (g_udev_device_get_devtype (udev_device), "disk") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "is not correct devtype=%s, expected disk",
			     g_udev_device_get_devtype (udev_device));
		return FALSE;
	}
	if (!g_udev_device_get_property_as_boolean (udev_device, "ID_ATA_SATA") ||
	    !g_udev_device_get_property_as_boolean (udev_device, "ID_ATA_DOWNLOAD_MICROCODE")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "has no ID_ATA_DOWNLOAD_MICROCODE");
		return FALSE;
	}

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (device, "scsi", error))
		return FALSE;

	/* look at the PCI and USB depth to work out if in an external enclosure */
	self->pci_depth = fu_udev_device_get_slot_depth (device, "pci");
	self->usb_depth = fu_udev_device_get_slot_depth (device, "usb");
	if (self->pci_depth <= 2 && self->usb_depth <= 2) {
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	}

	return TRUE;
}

static guint64
fu_ata_device_tf_to_pack_id (struct ata_tf *tf)
{
	guint32 lba24 = (tf->lbah << 16) | (tf->lbam << 8) | (tf->lbal);
	guint32 lbah = tf->dev & 0x0f;
	return (((guint64) lbah) << 24) | (guint64) lba24;
}

static gboolean
fu_ata_device_command (FuAtaDevice *self, struct ata_tf *tf,
		       gint dxfer_direction, guint timeout_ms,
		       guint8 *dxferp, gsize dxfer_len, GError **error)
{
	guint8 cdb[SG_ATA_12_LEN] = { 0x0 };
	guint8 sb[32] = { 0x0 };
	sg_io_hdr_t io_hdr = { 0x0 };

	/* map _TO_DEV to PIO mode */
	if (dxfer_direction == SG_DXFER_TO_DEV)
		cdb[1] = SG_ATA_PROTO_PIO_OUT;
	else if (dxfer_direction == SG_DXFER_FROM_DEV)
		cdb[1] = SG_ATA_PROTO_PIO_IN;
	else
		cdb[1] = SG_ATA_PROTO_NON_DATA;

	/* libata workaround: don't demand sense data for IDENTIFY */
	if (dxfer_len > 0) {
		cdb[2] |= SG_CDB2_TLEN_NSECT | SG_CDB2_TLEN_SECTORS;
		cdb[2] |= dxfer_direction == SG_DXFER_TO_DEV ? SG_CDB2_TDIR_TO_DEV : SG_CDB2_TDIR_FROM_DEV;
	} else {
		cdb[2] = SG_CDB2_CHECK_COND;
	}

	/* populate non-LBA48 CDB */
	cdb[0] = SG_ATA_12;
	cdb[3] = tf->feat;
	cdb[4] = tf->nsect;
	cdb[5] = tf->lbal;
	cdb[6] = tf->lbam;
	cdb[7] = tf->lbah;
	cdb[8] = tf->dev;
	cdb[9] = tf->command;
	if (g_getenv ("FWUPD_ATA_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "CBD", cdb, sizeof(cdb));
		if (dxfer_direction == SG_DXFER_TO_DEV && dxferp != NULL) {
			fu_common_dump_raw (G_LOG_DOMAIN, "outgoing_data",
					    dxferp, dxfer_len);
		}
	}

	/* hit hardware */
	io_hdr.interface_id	= 'S';
	io_hdr.mx_sb_len	= sizeof(sb);
	io_hdr.dxfer_direction	= dxfer_direction;
	io_hdr.dxfer_len	= dxfer_len;
	io_hdr.dxferp		= dxferp;
	io_hdr.cmdp		= cdb;
	io_hdr.cmd_len		= SG_ATA_12_LEN;
	io_hdr.sbp		= sb;
	io_hdr.pack_id		= fu_ata_device_tf_to_pack_id (tf);
	io_hdr.timeout		= timeout_ms;
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   SG_IO, (guint8 *) &io_hdr,
				   NULL, error))
		return FALSE;
	g_debug ("ATA_%u status=0x%x, host_status=0x%x, driver_status=0x%x",
		io_hdr.cmd_len, io_hdr.status, io_hdr.host_status, io_hdr.driver_status);
	if (g_getenv ("FWUPD_ATA_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "SB", sb, sizeof(sb));

	/* error check */
	if (io_hdr.status && io_hdr.status != SG_CHECK_CONDITION) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "bad status: 0x%x", io_hdr.status);
		return FALSE;
	}
	if (io_hdr.host_status) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "bad host status: 0x%x", io_hdr.host_status);
		return FALSE;
	}
	if (io_hdr.driver_status && (io_hdr.driver_status != SG_DRIVER_SENSE)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "bad driver status: 0x%x", io_hdr.driver_status);
		return FALSE;
	}

	/* repopulate ata_tf */
	tf->error  = sb[8 + 3];
	tf->nsect  = sb[8 + 5];
	tf->lbal   = sb[8 + 7];
	tf->lbam   = sb[8 + 9];
	tf->lbah   = sb[8 + 11];
	tf->dev    = sb[8 + 12];
	tf->status = sb[8 + 13];
	g_debug ("ATA_%u stat=%02x err=%02x nsect=%02x lbal=%02x lbam=%02x lbah=%02x dev=%02x",
		 io_hdr.cmd_len, tf->status, tf->error, tf->nsect, tf->lbal, tf->lbam, tf->lbah, tf->dev);

	/* io error */
	if (tf->status & (ATA_STAT_ERR | ATA_STAT_DRQ)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "I/O error, ata_op=0x%02x ata_status=0x%02x ata_error=0x%02x",
			     tf->command, tf->status, tf->error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ata_device_setup (FuDevice *device, GError **error)
{
	FuAtaDevice *self = FU_ATA_DEVICE (device);
	struct ata_tf tf = { 0x0 };
	guint8 id[FU_ATA_IDENTIFY_SIZE];

	/* get ID block */
	tf.dev = ATA_USING_LBA;
	tf.command = ATA_OP_IDENTIFY;
	tf.nsect = 1; /* 512 bytes */
	if (!fu_ata_device_command (self, &tf, SG_DXFER_FROM_DEV, 1000,
				    id, sizeof(id), error)) {
		g_prefix_error (error, "failed to IDENTIFY");
		return FALSE;
	}
	if (g_getenv ("FWUPD_ATA_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "IDENTIFY", id, sizeof(id));
	if (!fu_ata_device_parse_id (self, id, sizeof(id), error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_ata_device_activate (FuDevice *device, GError **error)
{
	FuAtaDevice *self = FU_ATA_DEVICE (device);
	struct ata_tf tf = { 0x0 };


	/* flush cache and put drive in standby to prepare to activate */
	tf.dev = ATA_USING_LBA;
	tf.command = ATA_OP_FLUSH_CACHE;
	if (!fu_ata_device_command (self, &tf, SG_DXFER_NONE,
				    120 * 1000, /* a long time! */
				    NULL, 0, error)) {
		g_prefix_error (error, "failed to flush cache immediate: ");
		return FALSE;
	}
	tf.command = ATA_OP_STANDBY_IMMEDIATE;
	if (!fu_ata_device_command (self, &tf, SG_DXFER_NONE,
				    120 * 1000, /* a long time! */
				    NULL, 0, error)) {
		g_prefix_error (error, "failed to standby immediate: ");
		return FALSE;
	}

	/* load the new firmware */
	tf.dev = 0xa0 | ATA_USING_LBA;
	tf.command = ATA_OP_DOWNLOAD_MICROCODE;
	tf.feat = ATA_SUBCMD_MICROCODE_ACTIVATE;
	if (!fu_ata_device_command (self, &tf, SG_DXFER_NONE,
				    120 * 1000, /* a long time! */
				    NULL, 0, error)) {
		g_prefix_error (error, "failed to activate firmware: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ata_device_fw_download (FuAtaDevice *self,
			   guint32 idx,
			   guint32 addr,
			   const guint8 *data,
			   guint32 data_sz,
			   GError **error)
{
	struct ata_tf tf = { 0x0 };
	guint32 block_count = data_sz / FU_ATA_BLOCK_SIZE;
	guint32 buffer_offset = addr / FU_ATA_BLOCK_SIZE;

	/* write block */
	tf.dev = 0xa0 | ATA_USING_LBA;
	tf.command = ATA_OP_DOWNLOAD_MICROCODE;
	tf.feat = self->transfer_mode;
	tf.nsect = block_count & 0xff;
	tf.lbal = block_count >> 8;
	tf.lbam = buffer_offset & 0xff;
	tf.lbah = buffer_offset >> 8;
	if (!fu_ata_device_command (self, &tf, SG_DXFER_TO_DEV,
				    120 * 1000, /* a long time! */
				    (guint8 *) data, data_sz, error)) {
		g_prefix_error (error, "failed to write firmware @0x%0x",
				(guint) addr);
		return FALSE;
	}

	/* check drive status */
	if (tf.nsect == 0x0)
		return TRUE;

	/* drive wants more data, or thinks it is all done */
	if (tf.nsect == 0x1 || tf.nsect == 0x2)
		return TRUE;

	/* the offset was set up incorrectly */
	if (tf.nsect == 0x4) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "alignment error");
		return FALSE;
	}

	/* other error */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_INVALID_DATA,
		     "unknown return code 0x%02x",
		     tf.nsect);
	return FALSE;
}

static gboolean
fu_ata_device_write_firmware (FuDevice *device,
			      FuFirmware *firmware,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuAtaDevice *self = FU_ATA_DEVICE (device);
	guint32 chunksz = (guint32) self->transfer_blocks * FU_ATA_BLOCK_SIZE;
	guint max_size = 0xffff * FU_ATA_BLOCK_SIZE;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* only one block allowed */
	if (self->transfer_mode == ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNK)
		max_size = 0xffff;

	/* check is valid */
	if (g_bytes_get_size (fw) > max_size) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "firmware is too large, maximum size is %u",
			     max_size);
		return FALSE;
	}
	if (g_bytes_get_size (fw) % FU_ATA_BLOCK_SIZE != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "firmware is not multiple of block size %i",
			     FU_ATA_BLOCK_SIZE);
		return FALSE;
	}

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (fw, 0x00, 0x00, chunksz);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_ata_device_fw_download (self,
						chk->idx,
						chk->address,
						chk->data,
						chk->data_sz,
						error)) {
			g_prefix_error (error, "failed to write chunk %u: ", i);
			return FALSE;
		}
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len + 1);
	}

	/* success! */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_progress (device, 100);
	return TRUE;
}

static gboolean
fu_ata_device_set_quirk_kv (FuDevice *device,
			    const gchar *key,
			    const gchar *value,
			    GError **error)
{
	FuAtaDevice *self = FU_ATA_DEVICE (device);
	if (g_strcmp0 (key, "AtaTransferMode") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp != ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS_ACTIVATE &&
		    tmp != ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS &&
		    tmp != ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNK) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "AtaTransferMode only supports "
					     "values 0x3, 0x7 or 0xe");
			return FALSE;
		}
		self->transfer_mode = (guint8) tmp;
		return TRUE;
	}
	if (g_strcmp0 (key, "AtaTransferBlocks") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp > 0xffff) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "AtaTransferBlocks only supports "
					     "values <= 0xffff");
			return FALSE;
		}
		self->transfer_blocks = (guint16) tmp;
		return TRUE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_ata_device_init (FuAtaDevice *self)
{
	/* we chose this default as _DOWNLOAD_CHUNKS_ACTIVATE applies the
	 * firmware straight away and the kernel might not like the unexpected
	 * ATA restart and panic */
	self->transfer_mode = ATA_SUBCMD_MICROCODE_DOWNLOAD_CHUNKS;
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_summary (FU_DEVICE (self), "ATA Drive");
	fu_device_add_icon (FU_DEVICE (self), "drive-harddisk");
	fu_device_set_protocol (FU_DEVICE (self), "org.t13.ata");
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self), FU_UDEV_DEVICE_FLAG_OPEN_READ);
}

static void
fu_ata_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_ata_device_parent_class)->finalize (object);
}

static void
fu_ata_device_class_init (FuAtaDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_ata_device_finalize;
	klass_device->to_string = fu_ata_device_to_string;
	klass_device->set_quirk_kv = fu_ata_device_set_quirk_kv;
	klass_device->setup = fu_ata_device_setup;
	klass_device->activate = fu_ata_device_activate;
	klass_device->write_firmware = fu_ata_device_write_firmware;
	klass_udev_device->probe = fu_ata_device_probe;
}

FuAtaDevice *
fu_ata_device_new_from_blob (const guint8 *buf, gsize sz, GError **error)
{
	g_autoptr(FuAtaDevice) self = g_object_new (FU_TYPE_ATA_DEVICE, NULL);
	if (!fu_ata_device_parse_id (self, buf, sz, error))
		return NULL;
	return g_steal_pointer (&self);
}
