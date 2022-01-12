/*
 * Copyright (C) 2021 Intel Corporation.
 * Copyright (C) 2021 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "fu-dell-dock-common.h"

#define GR_USB_INTERFACE_NUMBER 0x0
#define GR_USB_BLOCK_SIZE	64

/* bmRequest type */
#define USB_REQ_TYPE_GET_MMIO 0xc0 /* bm Request type */
#define USB_REQ_TYPE_SET_MMIO 0x40 /* bm Request type */

/* bRequest */
#define REQ_HUB_GET_MMIO 64
#define REQ_HUB_SET_MMIO 65

/* wValue*/
#define MBOX_ACCESS (1 << 10)

/* wIndex, mailbox register offset */
/* First 16 registers are Data[0]-Data[15] registers */
#define MBOX_REG_METADATA 16
#define MBOX_REG	  17 /* no name? */

/* mask for the MBOX_REG register that has no name */
#define MBOX_ERROR   (1 << 6) /* of the u8 status field */
#define MBOX_OPVALID (1 << 7) /* of the u8 status field */

#define MBOX_TIMEOUT 3000

/* HUB operation OP codes */
#define OP_NVM_WRITE	  0x20
#define OP_NVM_AUTH_WRITE 0x21
#define OP_NVM_READ	  0x22
#define OP_NVM_SET_OFFSET 0x23
#define OP_DROM_READ	  0x24

/* NVM metadata offset and length fields are in dword units */
/* note that these won't work for DROM read */
#define NVM_OFFSET_TO_METADATA(p) ((((p) / 4) & 0x3fffff) << 2) /* bits 23:2  */
#define NVM_LENGTH_TO_METADATA(p) ((((p) / 4) & 0xf) << 24)	/* bits 27:24 */

/* Default length for NVM READ */
#define NVM_READ_LENGTH 0x224

/* NVM offset */
#define NVM_VER_OFFSET_MAJOR 0xa
#define NVM_VER_OFFSET_MINOR 0x9
#define NVM_VID_OFFSET_MAJOR 0x221
#define NVM_VID_OFFSET_MINOR 0x220
#define NVM_PID_OFFSET_MAJOR 0x223
#define NVM_PID_OFFSET_MINOR 0x222

struct mbox_regx {
	guint16 opcode;
	guint8 rsvd;
	guint8 status;
} __attribute__((packed));

struct _FuDellDockUsb4 {
	FuUsbDevice parent_instance;
	guint blocksz;
	guint8 intf_nr;
};

G_DEFINE_TYPE(FuDellDockUsb4, fu_dell_dock_usb4, FU_TYPE_USB_DEVICE)

/* wIndex contains the hub register offset, value BIT[10] is "access to
 * mailbox", rest of values are vendor specific or rsvd  */
static gboolean
fu_dell_dock_usb4_hub_get_mmio(FuDevice *device, guint16 mbox_reg, guchar *buf, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	struct mbox_regx *regx;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   REQ_HUB_GET_MMIO, /* request */
					   MBOX_ACCESS,	     /* value */
					   mbox_reg,	     /* index */
					   (guchar *)buf,    /* data */
					   4,		     /* length */
					   NULL,	     /* actual length */
					   MBOX_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "GET_MMIO failed to set control on mbox register index [0x%x]: ",
			       mbox_reg);
		return FALSE;
	}
	/* verify status for specific hub mailbox register */
	if (mbox_reg == MBOX_REG) {
		regx = (struct mbox_regx *)buf;

		/* error status bit */
		if (regx->status & MBOX_ERROR) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "GET_MMIO opcode [0x%x] nonzero error bit in status [0x%x]",
				    regx->opcode,
				    regx->status);
			return FALSE;
		}

		/* operation valid (OV) bit should be 0'b */
		if (regx->status & MBOX_OPVALID) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "GET_MMIO opcode [0x%x] nonzero OV bit in status [0x%x]",
				    regx->opcode,
				    regx->status);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_dell_dock_usb4_hub_set_mmio(FuDevice *device, guint16 mbox_reg, guchar *buf, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   REQ_HUB_SET_MMIO, /* request */
					   MBOX_ACCESS,	     /* value */
					   mbox_reg,	     /* index */
					   (guchar *)buf,    /* data */
					   4,		     /* length */
					   NULL,	     /* actual length */
					   MBOX_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to set mmio 0x%x: ", mbox_reg);
		return FALSE;
	}
	return TRUE;
}

/*
 * Read up to 64 bytes of data from the mbox data registers to a buffer.
 * The mailbox can hold 64 bytes of data in 16 doubleword data registers.
 * To get data from NVM or DROM to mbox registers issue a NVM Read or DROM
 * read operation before reading the mbox data registers.
 */
static gboolean
fu_dell_dock_usb4_mbox_data_read(FuDevice *device, guchar *data, guint8 length, GError **error)
{
	guchar *ptr = data;

	if (length > 64 || length % 4) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "invalid firmware data read length %u",
			    length);
		return FALSE;
	}
	/* read 4 bytes per iteration */
	for (gint i = 0; i < length / 4; i++) {
		if (!fu_dell_dock_usb4_hub_get_mmio(device, i, ptr, error)) {
			g_prefix_error(error, "failed to read mbox data registers: ");
			return FALSE;
		}
		ptr += 4;
	}
	return TRUE;
}

/*
 * The mailbox can hold 64 bytes in 16 doubleword data registers.
 * A NVM write operation writes data from these registers to NVM
 * at the set offset
 */
static gboolean
fu_dell_dock_usb4_mbox_data_write(FuDevice *device,
				  const guchar *data,
				  guint8 length,
				  GError **error)
{
	guchar *ptr = (guchar *)data;

	if (length > 64 || length % 4) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "invalid firmware data write length %u",
			    length);
		return FALSE;
	}

	/* writes 4 bytes per iteration */
	for (gint i = 0; i < length / 4; i++) {
		if (!fu_dell_dock_usb4_hub_set_mmio(device, i, ptr, error))
			return FALSE;
		ptr += 4;
	}
	return TRUE;
}

static gboolean
fu_dell_dock_usb4_hub_operation(FuDevice *device, guint16 opcode, guchar *metadata, GError **error)
{
	struct mbox_regx *regx;
	gint max_tries = 100;
	guchar buf[4] = {0x0};

	regx = (struct mbox_regx *)buf;
	regx->opcode = GUINT16_TO_LE(opcode);
	regx->status = MBOX_OPVALID;

	/* Write metadata register for operations that use it */
	switch (opcode) {
	case OP_NVM_WRITE:
	case OP_NVM_AUTH_WRITE:
		break;
	case OP_NVM_READ:
	case OP_NVM_SET_OFFSET:
	case OP_DROM_READ:
		if (metadata == NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "hub opcode 0x%x requires metadata",
				    opcode);
			return FALSE;
		}
		if (!fu_dell_dock_usb4_hub_set_mmio(device, MBOX_REG_METADATA, metadata, error)) {
			g_prefix_error(error, "failed to write metadata %s: ", metadata);
			return FALSE;
		}
		break;
	default:
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "invalid hub opcode: 0x%x",
			    opcode);
		return FALSE;
	}

	/* write the operation and poll completion or error */
	if (!fu_dell_dock_usb4_hub_set_mmio(device, MBOX_REG, buf, error))
		return FALSE;

	/* leave early as successful USB4 AUTH resets the device immediately */
	if (opcode == OP_NVM_AUTH_WRITE)
		return TRUE;

	for (gint i = 0; i <= max_tries; i++) {
		g_autoptr(GError) error_local = NULL;
		if (fu_dell_dock_usb4_hub_get_mmio(device, MBOX_REG, buf, &error_local))
			return TRUE;
		if (i == max_tries) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "maximum tries exceeded: ");
		}
		g_usleep((gulong)10000);
	}
	return FALSE;
}

/**
 * fu_dell_dock_usb4_hub_nvm_read:
 * @device: a #FuDevice
 * @buf: array of bytes to store the read from registers
 * @length: length of buf array
 * @nvm_addr: nvm read offset, e.g. `0`
 * @error: (nullable): optional return location for an error
 *
 * Read NVM over USB interface
 **/
static gboolean
fu_dell_dock_usb4_hub_nvm_read(FuDevice *device,
			       guint8 *buf,
			       guint32 length,
			       guint32 nvm_addr,
			       GError **error)
{
	guint8 tmpbuf[64] = {0x0};

	while (length > 0) {
		guint32 unaligned_bytes = nvm_addr % 4;
		guint32 padded_len;
		guint32 nbytes;
		guint8 metadata[4];

		if (length + unaligned_bytes < 64) {
			nbytes = length;
			padded_len = unaligned_bytes + length;

			/* align end to full dword boundary */
			if (padded_len % 4)
				padded_len = (padded_len & ~0x3) + 4;
		} else {
			padded_len = 64;
			nbytes = padded_len - unaligned_bytes;
		}

		/* set nvm read offset in dwords */
		fu_common_write_uint32(metadata, NVM_OFFSET_TO_METADATA(nvm_addr), G_LITTLE_ENDIAN);

		/* and length field in dwords, note 0 means 16 dwords */
		metadata[3] = (padded_len / 4) & 0xf;

		/* ask hub to read up to 64 bytes from NVM to mbox data regs */
		if (!fu_dell_dock_usb4_hub_operation(device, OP_NVM_READ, metadata, error)) {
			g_prefix_error(error, "hub NVM read error: ");
			return FALSE;
		}
		/* read the data from mbox data regs into our buffer */
		if (!fu_dell_dock_usb4_mbox_data_read(device, tmpbuf, padded_len, error)) {
			g_prefix_error(error, "hub firmware mbox data read error: ");
			return FALSE;
		}
		if (!fu_memcpy_safe(buf,
				    length,
				    0x0,
				    tmpbuf,
				    sizeof(tmpbuf),
				    unaligned_bytes,
				    nbytes,
				    error))
			return FALSE;

		buf += nbytes;
		nvm_addr += nbytes;
		length -= nbytes;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_usb4_hub_nvm_write(FuDevice *device,
				const guint8 *buf,
				guint32 length,
				guint32 nvm_addr,
				FuProgress *progress,
				GError **error)
{
	guint8 metadata[4];
	guint32 bytes_done = 0;
	guint32 bytes_total = length;

	if (nvm_addr % 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Invalid NVM write offset 0x%x, must be DW aligned: ",
			    nvm_addr);
		return FALSE;
	}
	if (length < 64 || length % 64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Invalid NVM length 0x%x, must be 64 byte aligned: ",
			    length);
		return FALSE;
	}

	/* 1. Set initial offset, must be DW aligned */
	fu_common_write_uint32(metadata, NVM_OFFSET_TO_METADATA(nvm_addr), G_LITTLE_ENDIAN);

	if (!fu_dell_dock_usb4_hub_operation(device, OP_NVM_SET_OFFSET, metadata, error)) {
		g_prefix_error(error, "hub NVM set offset error: ");
		return FALSE;
	}

	/* 2 Write data in 64 byte blocks */
	fu_progress_set_percentage_full(progress, bytes_done, bytes_total);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	while (length > 0) {
		/* write data to mbox data regs */
		if (!fu_dell_dock_usb4_mbox_data_write(device, buf, 64, error)) {
			g_prefix_error(error, "hub mbox data write error: ");
			return FALSE;
		}
		/* ask hub to write 64 bytes from data regs to NVM */
		if (!fu_dell_dock_usb4_hub_operation(device, OP_NVM_WRITE, NULL, error)) {
			g_prefix_error(error, "hub NVM write operation error: ");
			return FALSE;
		}
		buf += 64;
		length -= 64;
		fu_progress_set_percentage_full(progress, bytes_done += 64, bytes_total);
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
	return TRUE;
}

static gboolean
fu_dell_dock_usb4_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_dell_dock_usb4_hub_operation(device, OP_NVM_AUTH_WRITE, NULL, error)) {
		g_prefix_error(error, "NVM authenticate failed: ");
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
		return FALSE;
	}
	fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
	return TRUE;
}

static gboolean
fu_dell_dock_usb4_write_fw(FuDevice *device,
			   FuFirmware *firmware,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	const guint8 *fw_buf;
	gsize fw_blob_size = 0;
	guchar nvm_buf[NVM_READ_LENGTH] = {0x0};
	guint32 fw_header_offset = 0;
	guint8 *tmp_header = NULL;
	g_autofree gchar *fw_product_id = NULL;
	g_autofree gchar *fw_vendor_id = NULL;
	g_autofree gchar *fw_version = NULL;
	g_autofree gchar *nvm_product_id = NULL;
	g_autofree gchar *nvm_vendor_id = NULL;
	g_autoptr(GBytes) fw_image = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default image */
	fw_image = fu_firmware_get_bytes(firmware, error);
	if (fw_image == NULL)
		return FALSE;

	fw_buf = g_bytes_get_data(fw_image, &fw_blob_size);
	g_debug("total image size: %" G_GSIZE_FORMAT, fw_blob_size);

	/* get header offset */
	tmp_header = (guint8 *)&fw_header_offset;
	if (!fu_memcpy_safe(tmp_header,
			    sizeof(guint32),
			    0x0,
			    fw_buf,
			    fw_blob_size,
			    0x0,
			    sizeof(guint32),
			    error))
		return FALSE;

	g_debug("image header size: %" G_GUINT32_FORMAT, fw_header_offset);
	if (fw_header_offset > fw_blob_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "image header is too big: %" G_GUINT32_FORMAT,
			    fw_header_offset);
		return FALSE;
	}

	/* get firmware version, vendor-id, product-id */
	fw_version = g_strdup_printf("%02x.%02x",
				     fw_buf[fw_header_offset + NVM_VER_OFFSET_MAJOR],
				     fw_buf[fw_header_offset + NVM_VER_OFFSET_MINOR]);
	fw_vendor_id = g_strdup_printf("%02x%02x",
				       fw_buf[fw_header_offset + NVM_VID_OFFSET_MAJOR],
				       fw_buf[fw_header_offset + NVM_VID_OFFSET_MINOR]);
	fw_product_id = g_strdup_printf("%02x%02x",
					fw_buf[fw_header_offset + NVM_PID_OFFSET_MAJOR],
					fw_buf[fw_header_offset + NVM_PID_OFFSET_MINOR]);

	g_debug("writing Thunderbolt firmware version %s", fw_version);
	g_debug("writing Thunderbolt product-id %s", fw_product_id);
	g_debug("writing Thunderbolt vendor-id %s", fw_vendor_id);

	/* compare vendor-id, product-id between firmware blob and NVM */
	if (!fu_dell_dock_usb4_hub_nvm_read(device, nvm_buf, NVM_READ_LENGTH, 0, error)) {
		g_prefix_error(error, "NVM READ error: ");
		return FALSE;
	}
	nvm_vendor_id = g_strdup_printf("%02x%02x",
					nvm_buf[NVM_VID_OFFSET_MAJOR],
					nvm_buf[NVM_VID_OFFSET_MINOR]);
	nvm_product_id = g_strdup_printf("%02x%02x",
					 nvm_buf[NVM_PID_OFFSET_MAJOR],
					 nvm_buf[NVM_PID_OFFSET_MINOR]);

	if (((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) &&
	    (g_strcmp0(nvm_vendor_id, fw_vendor_id) != 0 ||
	     g_strcmp0(nvm_product_id, fw_product_id) != 0)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Thunderbolt firmware vendor_id %s, product_id %s"
			    "doesn't match NVM vendor_id %s, product_id %s",
			    fw_vendor_id,
			    fw_product_id,
			    nvm_vendor_id,
			    nvm_product_id);
		return FALSE;
	}

	/* firmware install */
	fw_buf += fw_header_offset;
	fw_blob_size -= fw_header_offset;
	if (!fu_dell_dock_usb4_hub_nvm_write(device, fw_buf, fw_blob_size, 0, progress, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_version(device, fw_version);
	return TRUE;
}

static gboolean
fu_dell_dock_usb4_setup(FuDevice *device, GError **error)
{
	guchar buf[NVM_READ_LENGTH] = {0x0};
	g_autofree gchar *name = NULL;
	g_autofree gchar *nvm_product_id = NULL;
	g_autofree gchar *nvm_vendor_id = NULL;
	g_autofree gchar *nvm_version = NULL;

	if (!fu_dell_dock_usb4_hub_nvm_read(device, buf, NVM_READ_LENGTH, 0, error)) {
		g_prefix_error(error, "NVM READ error: ");
		return FALSE;
	}
	nvm_version =
	    g_strdup_printf("%02x.%02x", buf[NVM_VER_OFFSET_MAJOR], buf[NVM_VER_OFFSET_MINOR]),
	nvm_vendor_id =
	    g_strdup_printf("%02x%02x", buf[NVM_VID_OFFSET_MAJOR], buf[NVM_VID_OFFSET_MINOR]);
	nvm_product_id =
	    g_strdup_printf("%02x%02x", buf[NVM_PID_OFFSET_MAJOR], buf[NVM_PID_OFFSET_MINOR]);

	/* only add known supported thunderbolt devices */
	name = g_strdup_printf("TBT-%s%s", nvm_vendor_id, nvm_product_id);
	if (g_strcmp0(name, DELL_DOCK_USB4_INSTANCE_ID) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no supported device found");
		return FALSE;
	}
	fu_device_add_instance_id(device, name);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_version(device, nvm_version);
	return TRUE;
}

static gboolean
fu_dell_dock_usb4_probe(FuDevice *device, GError **error)
{
	FuDellDockUsb4 *self = FU_DELL_DOCK_USB4(device);

	self->intf_nr = GR_USB_INTERFACE_NUMBER;
	self->blocksz = GR_USB_BLOCK_SIZE;
	fu_device_set_logical_id(FU_DEVICE(device), "usb4");
	return TRUE;
}

static void
fu_dell_dock_usb4_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_dell_dock_usb4_parent_class)->finalize(object);
}

static void
fu_dell_dock_usb4_init(FuDellDockUsb4 *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.thunderbolt");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_INHERIT_ACTIVATION);
}

static void
fu_dell_dock_usb4_class_init(FuDellDockUsb4Class *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_dock_usb4_finalize;
	klass_device->probe = fu_dell_dock_usb4_probe;
	klass_device->setup = fu_dell_dock_usb4_setup;
	klass_device->write_firmware = fu_dell_dock_usb4_write_fw;
	klass_device->activate = fu_dell_dock_usb4_activate;
}

/**
 * fu_dell_dock_usb4_new:
 *
 * Creates a new USB4 device object.
 *
 * Returns: a new #FuDellDockUsb4
 **/
FuDellDockUsb4 *
fu_dell_dock_usb4_new(FuUsbDevice *device)
{
	FuDellDockUsb4 *self = g_object_new(FU_TYPE_DELL_DOCK_USB4, NULL);
	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device));
	return FU_DELL_DOCK_USB4(self);
}
