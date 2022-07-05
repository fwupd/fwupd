/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include "fu-intel-usb4-device.h"
#include "fu-intel-usb4-firmware.h"
#include "fu-intel-usb4-nvm.h"

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

struct mbox_regx {
	guint16 opcode;
	guint8 rsvd;
	guint8 status;
} __attribute__((packed));

struct _FuIntelUsb4Device {
	FuUsbDevice parent_instance;
	guint blocksz;
	guint8 intf_nr;
	guint16 nvm_product_id;
	guint16 nvm_vendor_id;
};

G_DEFINE_TYPE(FuIntelUsb4Device, fu_intel_usb4_device, FU_TYPE_USB_DEVICE)

/* wIndex contains the hub register offset, value BIT[10] is "access to
 * mailbox", rest of values are vendor specific or rsvd  */
static gboolean
fu_intel_usb4_device_get_mmio(FuDevice *device, guint16 mbox_reg, guint8 *buf, GError **error)
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
					   (guint8 *)buf,    /* data */
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
fu_intel_usb4_device_set_mmio(FuDevice *device, guint16 mbox_reg, guint8 *buf, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   REQ_HUB_SET_MMIO, /* request */
					   MBOX_ACCESS,	     /* value */
					   mbox_reg,	     /* index */
					   (guint8 *)buf,    /* data */
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
fu_intel_usb4_device_mbox_data_read(FuDevice *device, guint8 *data, guint8 length, GError **error)
{
	guint8 *ptr = data;

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
		if (!fu_intel_usb4_device_get_mmio(device, i, ptr, error)) {
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
fu_intel_usb4_device_mbox_data_write(FuDevice *device,
				     const guint8 *data,
				     guint8 length,
				     GError **error)
{
	guint8 *ptr = (guint8 *)data;

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
		if (!fu_intel_usb4_device_set_mmio(device, i, ptr, error))
			return FALSE;
		ptr += 4;
	}
	return TRUE;
}

static gboolean
fu_intel_usb4_device_operation(FuDevice *device, guint16 opcode, guint8 *metadata, GError **error)
{
	struct mbox_regx *regx;
	gint max_tries = 100;
	guint8 buf[4] = {0x0};

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
		if (!fu_intel_usb4_device_set_mmio(device, MBOX_REG_METADATA, metadata, error)) {
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
	if (!fu_intel_usb4_device_set_mmio(device, MBOX_REG, buf, error))
		return FALSE;

	/* leave early as successful USB4 AUTH resets the device immediately */
	if (opcode == OP_NVM_AUTH_WRITE)
		return TRUE;

	for (gint i = 0; i <= max_tries; i++) {
		g_autoptr(GError) error_local = NULL;
		if (fu_intel_usb4_device_get_mmio(device, MBOX_REG, buf, &error_local))
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

static gboolean
fu_intel_usb4_device_nvm_read(FuDevice *device,
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
		fu_memwrite_uint32(metadata, NVM_OFFSET_TO_METADATA(nvm_addr), G_LITTLE_ENDIAN);

		/* and length field in dwords, note 0 means 16 dwords */
		metadata[3] = (padded_len / 4) & 0xf;

		/* ask hub to read up to 64 bytes from NVM to mbox data regs */
		if (!fu_intel_usb4_device_operation(device, OP_NVM_READ, metadata, error)) {
			g_prefix_error(error, "hub NVM read error: ");
			return FALSE;
		}
		/* read the data from mbox data regs into our buffer */
		if (!fu_intel_usb4_device_mbox_data_read(device, tmpbuf, padded_len, error)) {
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
fu_intel_usb4_device_nvm_write(FuDevice *device,
			       GBytes *blob,
			       guint32 nvm_addr,
			       FuProgress *progress,
			       GError **error)
{
	guint8 metadata[4];
	g_autoptr(GPtrArray) chunks = NULL;

	if (nvm_addr % 4 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Invalid NVM write offset 0x%x, must be DW aligned: ",
			    nvm_addr);
		return FALSE;
	}
	if (g_bytes_get_size(blob) < 64 || g_bytes_get_size(blob) % 64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Invalid NVM length 0x%x, must be 64 byte aligned: ",
			    (guint)g_bytes_get_size(blob));
		return FALSE;
	}

	/* set initial offset, must be DW aligned */
	fu_memwrite_uint32(metadata, NVM_OFFSET_TO_METADATA(nvm_addr), G_LITTLE_ENDIAN);
	if (!fu_intel_usb4_device_operation(device, OP_NVM_SET_OFFSET, metadata, error)) {
		g_prefix_error(error, "hub NVM set offset error: ");
		return FALSE;
	}

	/* write data in 64 byte blocks */
	chunks = fu_chunk_array_new_from_bytes(blob, 0x0, 0x0, 64);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		/* write data to mbox data regs */
		if (!fu_intel_usb4_device_mbox_data_write(device,
							  fu_chunk_get_data(chk),
							  fu_chunk_get_data_sz(chk),
							  error)) {
			g_prefix_error(error, "hub mbox data write error: ");
			return FALSE;
		}
		/* ask hub to write 64 bytes from data regs to NVM */
		if (!fu_intel_usb4_device_operation(device, OP_NVM_WRITE, NULL, error)) {
			g_prefix_error(error, "hub NVM write operation error: ");
			return FALSE;
		}

		/* done */
		fu_progress_step_done(progress);
	}

	/* success */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
	return TRUE;
}

static gboolean
fu_intel_usb4_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_intel_usb4_device_operation(device, OP_NVM_AUTH_WRITE, NULL, error)) {
		g_prefix_error(error, "NVM authenticate failed: ");
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
		return FALSE;
	}
	fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
	return TRUE;
}

static FuFirmware *
fu_intel_usb4_device_prepare_firmware(FuDevice *device,
				      GBytes *fw,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuIntelUsb4Device *self = FU_INTEL_USB4_DEVICE(device);
	guint16 fw_vendor_id;
	guint16 fw_product_id;
	g_autoptr(FuFirmware) firmware = fu_intel_usb4_firmware_new();

	/* get vid:pid:rev */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* check is compatible */
	fw_vendor_id = fu_intel_usb4_nvm_get_vendor_id(FU_INTEL_USB4_NVM(firmware));
	fw_product_id = fu_intel_usb4_nvm_get_product_id(FU_INTEL_USB4_NVM(firmware));
	if (self->nvm_vendor_id != fw_vendor_id || self->nvm_product_id != fw_product_id) {
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware 0x%04x:0x%04x does not match device 0x%04x:0x%04x",
				    fw_vendor_id,
				    fw_product_id,
				    self->nvm_vendor_id,
				    self->nvm_product_id);
			return NULL;
		}
		g_warning("firmware 0x%04x:0x%04x does not match device 0x%04x:0x%04x",
			  fw_vendor_id,
			  fw_product_id,
			  self->nvm_vendor_id,
			  self->nvm_product_id);
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_intel_usb4_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(GBytes) fw_image = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get payload */
	fw_image = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (fw_image == NULL)
		return FALSE;

	/* firmware install */
	if (!fu_intel_usb4_device_nvm_write(device, fw_image, 0, progress, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_version(device, fu_firmware_get_version(firmware));
	return TRUE;
}

static gboolean
fu_intel_usb4_device_setup(FuDevice *device, GError **error)
{
	FuIntelUsb4Device *self = FU_INTEL_USB4_DEVICE(device);
	guint8 buf[NVM_READ_LENGTH] = {0x0};
	g_autofree gchar *name = NULL;
	g_autoptr(FuFirmware) fw = fu_intel_usb4_nvm_new();
	g_autoptr(GBytes) blob = NULL;

	/* read from device and parse firmware */
	if (!fu_intel_usb4_device_nvm_read(device, buf, sizeof(buf), 0, error)) {
		g_prefix_error(error, "NVM read error: ");
		return FALSE;
	}
	blob = g_bytes_new(buf, sizeof(buf));
	if (!fu_firmware_parse(fw, blob, FWUPD_INSTALL_FLAG_NONE, error)) {
		g_prefix_error(error, "NVM parse error: ");
		return FALSE;
	}
	self->nvm_vendor_id = fu_intel_usb4_nvm_get_vendor_id(FU_INTEL_USB4_NVM(fw));
	self->nvm_product_id = fu_intel_usb4_nvm_get_product_id(FU_INTEL_USB4_NVM(fw));

	name = g_strdup_printf("TBT-%04x%04x", self->nvm_vendor_id, self->nvm_product_id);
	fu_device_add_instance_id(device, name);
	fu_device_set_version(device, fu_firmware_get_version(fw));
	return TRUE;
}

static void
fu_intel_usb4_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIntelUsb4Device *self = FU_INTEL_USB4_DEVICE(device);
	fu_string_append_kx(str, idt, "NvmVendorId", self->nvm_vendor_id);
	fu_string_append_kx(str, idt, "NvmProductId", self->nvm_product_id);
}

static void
fu_intel_usb4_device_init(FuIntelUsb4Device *self)
{
	self->intf_nr = GR_USB_INTERFACE_NUMBER;
	self->blocksz = GR_USB_BLOCK_SIZE;
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.thunderbolt");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_INHERIT_ACTIVATION);
}

static void
fu_intel_usb4_device_class_init(FuIntelUsb4DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_intel_usb4_device_to_string;
	klass_device->setup = fu_intel_usb4_device_setup;
	klass_device->prepare_firmware = fu_intel_usb4_device_prepare_firmware;
	klass_device->write_firmware = fu_intel_usb4_device_write_firmware;
	klass_device->activate = fu_intel_usb4_device_activate;
}
