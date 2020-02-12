/*
 * Copyright (C) 2005-2019 Synaptics Incorporated
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-srec-firmware.h"

#include "fu-synaptics-cxaudio-common.h"
#include "fu-synaptics-cxaudio-device.h"
#include "fu-synaptics-cxaudio-firmware.h"

struct _FuSynapticsCxaudioDevice
{
	FuUsbDevice		 parent_instance;
	guint32			 chip_id_base;
	guint32			 chip_id;
	gboolean		 serial_number_set;
	gboolean		 sw_reset_supported;
	guint32			 eeprom_layout_version;
	guint32			 eeprom_patch2_valid_addr;
	guint32			 eeprom_patch_valid_addr;
	guint32			 eeprom_storage_address;
	guint32			 eeprom_storage_sz;
	guint32			 eeprom_sz;
	guint8			 patch_level;
};

G_DEFINE_TYPE (FuSynapticsCxaudioDevice, fu_synaptics_cxaudio_device, FU_TYPE_USB_DEVICE)

static void
fu_synaptics_cxaudio_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE (device);
	fu_common_string_append_ku (str, idt, "ChipIdBase", self->chip_id_base);
	fu_common_string_append_ku (str, idt, "ChipId", self->chip_id);
	fu_common_string_append_kx (str, idt, "EepromLayoutVersion", self->eeprom_layout_version);
	fu_common_string_append_kx (str, idt, "EepromStorageAddress", self->eeprom_storage_address);
	fu_common_string_append_kx (str, idt, "EepromStorageSz", self->eeprom_storage_sz);
	fu_common_string_append_kx (str, idt, "EepromSz", self->eeprom_sz);
	fu_common_string_append_kb (str, idt, "SwResetSupported", self->sw_reset_supported);
	fu_common_string_append_kb (str, idt, "SerialNumberSet", self->serial_number_set);
}

static gboolean
fu_synaptics_cxaudio_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* get firmware version */
	if (!g_usb_device_claim_interface (usb_device, FU_SYNAPTICS_CXAUDIO_HID_INTERFACE,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_output_report (FuSynapticsCxaudioDevice *self,
					   guint8 *buf,
					   guint16 bufsz,
					   GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 report_number = buf[0];
	gsize actual_length = 0;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz != 0, FALSE);

	/* weird */
	if (report_number == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "report 0 not supported");
		return FALSE;
	}

	/* to device */
	if (g_getenv ("FWUPD_SYNAPTICS_CXAUDIO_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "HID::WRITE", buf, bufsz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET,
					    (FU_HID_REPORT_TYPE_OUTPUT << 8) | report_number,
					    FU_SYNAPTICS_CXAUDIO_HID_INTERFACE,
					    buf, bufsz, &actual_length,
					    FU_SYNAPTICS_CXAUDIO_USB_TIMEOUT, NULL, error)) {
		return FALSE;
	}
	if (bufsz != actual_length) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "wrote 0x%x bytes of 0x%x",
			     (guint) actual_length, bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_input_report (FuSynapticsCxaudioDevice *self,
					  guint8 ReportID,
					  guint8 *buf,
					  guint16 bufsz,
					  GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_length = 0;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz != 0, FALSE);

	/* from device */
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_GET,
					    (FU_HID_REPORT_TYPE_INPUT << 8) | ReportID,
					    FU_SYNAPTICS_CXAUDIO_HID_INTERFACE,
					    buf, bufsz, &actual_length,
					    FU_SYNAPTICS_CXAUDIO_USB_TIMEOUT, NULL, error)) {
		return FALSE;
	}
	if (g_getenv ("FWUPD_SYNAPTICS_CXAUDIO_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "HID::READ", buf, bufsz);
	if (bufsz != actual_length) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "read 0x%x bytes of expected 0x%x",
			     (guint) actual_length, bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef enum {
	FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
	FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
	FU_SYNAPTICS_CXAUDIO_OPERATION_LAST
} FuSynapticsCxaudioOperation;

typedef enum {
	FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE		= 0,
	FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY		= (1 << 4),
} FuSynapticsCxaudioOperationFlags;

static gboolean
fu_synaptics_cxaudio_device_operation (FuSynapticsCxaudioDevice *self,
				       FuSynapticsCxaudioOperation operation,
				       FuSynapticsCxaudioMemKind mem_kind,
				       guint32 addr,
				       guint8 *buf,
				       guint32 bufsz,
				       FuSynapticsCxaudioOperationFlags flags,
				       GError **error)
{
	const guint32 idx_read = 0x1;
	const guint32 idx_write = 0x5;
	const guint32 payload_max = 0x20;
	guint32 size = 0x02800;
	g_autoptr(GPtrArray) chunks = NULL;

	g_return_val_if_fail (bufsz > 0, FALSE);
	g_return_val_if_fail (buf != NULL, FALSE);

	/* check if memory operation is supported by device */
	if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE &&
	    mem_kind == FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_ROM) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "trying to write unwritable section %u",
			     mem_kind);
		return FALSE;
	}

	/* check memory address - should be within valid range */
	if (mem_kind == FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM)
		size = 0x20000;
	if (addr > size) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "address out of range 0x%x < 0x%x",
			     addr, size);
		return FALSE;
	}

	/* send to hardware */
	chunks = fu_chunk_array_new (buf, bufsz, addr, 0x0, payload_max);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chunk = g_ptr_array_index (chunks, i);
		guint8 inbuf[FU_SYNAPTICS_CXAUDIO_INPUT_REPORT_SIZE] = { 0 };
		guint8 outbuf[FU_SYNAPTICS_CXAUDIO_OUTPUT_REPORT_SIZE] = { 0 };

		/* first byte is always report ID */
		outbuf[0] = FU_SYNAPTICS_CXAUDIO_MEM_WRITEID;

		/* set memory address and payload length (if relevant) */
		if (chunk->address >= 64 * 1024)
			outbuf[1] |= 1 << 4;
		outbuf[2] = chunk->data_sz;
		fu_common_write_uint16 (outbuf + 3, chunk->address, G_BIG_ENDIAN);

		/* set memtype */
		if (mem_kind == FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM)
			outbuf[1] |= 1 << 5;

		/* fill the report payload part */
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE) {
			outbuf[1] |= 1 << 6;
			if (!fu_memcpy_safe (outbuf, sizeof(outbuf), idx_write, /* dst */
					     chunk->data, chunk->data_sz, 0x0, /* src */
					     chunk->data_sz, error))
				return FALSE;
		}
		if (!fu_synaptics_cxaudio_device_output_report (self, outbuf, sizeof(outbuf), error))
			return FALSE;

		/* issue additional write directive to read */
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE &&
		    flags & FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY) {
			outbuf[1] &= ~(1 << 6);
			if (!fu_synaptics_cxaudio_device_output_report (self, outbuf, sizeof(outbuf), error))
				return FALSE;
		}
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_READ ||
		    flags & FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY) {
			if (!fu_synaptics_cxaudio_device_input_report (self,
							       FU_SYNAPTICS_CXAUDIO_MEM_READID,
							       inbuf, sizeof(inbuf),
							       error))
				return FALSE;
		}
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE &&
		    flags & FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY) {
			if (!fu_common_bytes_compare_raw (outbuf + idx_write, payload_max,
							  inbuf + idx_read, payload_max,
							  error)) {
				g_prefix_error (error,
						"failed to verify on packet %u @0x%x: ",
						chunk->idx, chunk->address);
				return FALSE;
			}
		}
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_READ) {
			if (!fu_memcpy_safe ((guint8 *) chunk->data, chunk->data_sz, 0x0, /* dst */
					     inbuf, sizeof(inbuf), idx_read, /* src */
					     chunk->data_sz, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_register_clear_bit (FuSynapticsCxaudioDevice *self,
						guint32 address,
						guint8 bit_position,
						GError **error)
{
	guint8 tmp = 0x0;
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						    address, &tmp, sizeof(tmp),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error))
		return FALSE;
	tmp &= ~(1 << bit_position);
	return fu_synaptics_cxaudio_device_operation (self,
						      FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
						      FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						      address,
						      &tmp, sizeof(guint8),
						      FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						      error);
}

static gboolean
fu_synaptics_cxaudio_device_register_set_bit (FuSynapticsCxaudioDevice *self,
					      guint32 address,
					      guint8 bit_position,
					      GError **error)
{
	guint8 tmp = 0x0;
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						    address, &tmp, sizeof(tmp),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error))
		return FALSE;
	tmp |= 1 << bit_position;
	return fu_synaptics_cxaudio_device_operation (self,
						      FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
						      FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						      address, &tmp, sizeof(tmp),
						      FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						      error);
}

static gchar *
fu_synaptics_cxaudio_device_eeprom_read_string (FuSynapticsCxaudioDevice *self,
						guint32 address,
						GError **error)
{
	FuSynapticsCxaudioEepromStringHeader header = { 0 };
	g_autofree gchar *str = NULL;

	/* read header */
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    address,
						    (guint8 *) &header, sizeof(header),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error,
				"failed to read EEPROM string header @0x%x: ",
				address);
		return NULL;
	}

	/* sanity check */
	if (header.Type != FU_SYNAPTICS_CXAUDIO_DEVICE_CAPABILITIES_BYTE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "EEPROM string header type invalid");
		return NULL;
	}
	if (header.Length < sizeof(header)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "EEPROM string header length invalid");
		return NULL;
	}

	/* allocate buffer + NUL terminator */
	str = g_malloc0 (header.Length - sizeof(header) + 1);
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    address + sizeof(header),
						    (guint8 *) str,
						    header.Length - sizeof(header),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error,
				"failed to read EEPROM string @0x%x: ",
				address);
		return NULL;
	}
	return g_steal_pointer (&str);
}

static gboolean
fu_synaptics_cxaudio_device_reset (FuSynapticsCxaudioDevice *self, GError **error)
{
	guint8 tmp = 1 << 6;
	g_autoptr(GError) error_local = NULL;

	/* is disabled on EVK board using jumper */
	if (!self->sw_reset_supported) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "software reset is not supported");
		return FALSE;
	}

	/* this fails on success */
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						    FU_SYNAPTICS_CXAUDIO_REG_RESET_ADDR, &tmp, sizeof(tmp),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_FAILED)) {
			return TRUE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_ensure_patch_level (FuSynapticsCxaudioDevice *self, GError **error)
{
	guint8 tmp = 0x0;
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    self->eeprom_patch_valid_addr,
						    &tmp, sizeof(tmp),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read EEPROM patch validation byte: ");
		return FALSE;
	}
	if (tmp == FU_SYNAPTICS_CXAUDIO_SIGNATURE_PATCH_BYTE) {
		self->patch_level = 1;
		return TRUE;
	}
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    self->eeprom_patch2_valid_addr,
						    &tmp, sizeof(tmp),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read EEPROM patch validation byte: ");
		return FALSE;
	}
	if (tmp == FU_SYNAPTICS_CXAUDIO_SIGNATURE_PATCH_BYTE) {
		self->patch_level = 2;
		return TRUE;
	}

	/* not sure what to do here */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "EEPROM patch version undiscoverable");
	return FALSE;
}

static gboolean
fu_synaptics_cxaudio_device_setup (FuDevice *device, GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	FuSynapticsCxaudioEepromCustomInfo cinfo = { 0x0 };
	guint32 addr;
	guint8 chip_id_offset = 0x0;
	guint8 sigbuf[2] = { 0x0 };
	guint8 verbuf_fw[4] = { 0x0 };
	guint8 verbuf_patch[3] = { 0x0 };
	g_autofree gchar *cap_str = NULL;
	g_autofree gchar *chip_id = NULL;
	g_autofree gchar *summary = NULL;
	g_autofree gchar *version_fw = NULL;
	g_autofree gchar *version_patch = NULL;

	/* get the ChipID */
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						    0x1005,
						    &chip_id_offset, sizeof(chip_id_offset),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read ChipID: ");
		return FALSE;
	}
	self->chip_id = self->chip_id_base + chip_id_offset;
	chip_id = g_strdup_printf ("SYNAPTICS_CXAUDIO\\CX%u", self->chip_id);
	fu_device_add_instance_id (device, chip_id);

	/* set summary */
	summary = g_strdup_printf ("CX%u USB audio device", self->chip_id);
	fu_device_set_summary (device, summary);

	/* read the EEPROM validity signature */
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    FU_SYNAPTICS_CXAUDIO_EEPROM_VALIDITY_SIGNATURE_OFFSET,
						    sigbuf, sizeof(sigbuf),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read EEPROM signature bytes: ");
		return FALSE;
	}

	/* blank EEPROM */
	if (sigbuf[0] == 0xff && sigbuf[1] == 0xff) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "EEPROM is missing or blank");
		return FALSE;
	}

	/* is disabled on EVK board using jumper */
	if ((sigbuf[0] == 0x00 && sigbuf[1] == 0x00) ||
	    (sigbuf[0] == 0xff && sigbuf[1] == 0x00)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "EEPROM has been disabled using a jumper");
		return FALSE;
	}

	/* check magic byte */
	if (sigbuf[0] != FU_SYNAPTICS_CXAUDIO_MAGIC_BYTE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "EEPROM magic byte invalid, got 0x%02x expected 0x%02x",
			     sigbuf[0], (guint) FU_SYNAPTICS_CXAUDIO_MAGIC_BYTE);
		return FALSE;
	}

	/* calculate EEPROM size */
	self->eeprom_sz = (guint32) 1 << (sigbuf[1] + 8);
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_SIZE_ADDRESS,
						    sigbuf, sizeof(sigbuf),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read EEPROM signature bytes: ");
		return FALSE;
	}
	self->eeprom_storage_sz = fu_common_read_uint16 (sigbuf, G_LITTLE_ENDIAN);
	if (self->eeprom_storage_sz < self->eeprom_sz - FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_PADDING_SIZE) {
		self->eeprom_storage_address = self->eeprom_sz - \
						self->eeprom_storage_sz - \
						FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_PADDING_SIZE;
	}

	/* get EEPROM custom info */
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET,
						    (guint8 *) &cinfo, sizeof(cinfo),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read EEPROM custom info: ");
		return FALSE;
	}
	if (cinfo.LayoutSignature == FU_SYNAPTICS_CXAUDIO_SIGNATURE_BYTE)
		self->eeprom_layout_version = cinfo.LayoutVersion;
	g_debug ("CpxPatchVersion: %u.%u.%u",
		 cinfo.CpxPatchVersion[0],
		 cinfo.CpxPatchVersion[1],
		 cinfo.CpxPatchVersion[2]);
	g_debug ("SpxPatchVersion: %u.%u.%u.%u",
		 cinfo.SpxPatchVersion[0],
		 cinfo.SpxPatchVersion[1],
		 cinfo.SpxPatchVersion[2],
		 cinfo.SpxPatchVersion[3]);
	g_debug ("VendorID: 0x%04x", cinfo.VendorID);
	g_debug ("ProductID: 0x%04x", cinfo.ProductID);
	g_debug ("RevisionID: 0x%04x", cinfo.RevisionID);
	g_debug ("ApplicationStatus: 0x%02x", cinfo.ApplicationStatus);

	/* serial number, which also allows us to recover it after write */
	if (self->eeprom_layout_version >= 0x01) {
		self->serial_number_set = cinfo.SerialNumberStringAddress != 0x0;
		if (self->serial_number_set) {
			g_autofree gchar *tmp = NULL;
			tmp = fu_synaptics_cxaudio_device_eeprom_read_string (self,
									      cinfo.SerialNumberStringAddress,
									      error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_serial (device, tmp);
		}
	}

	/* read fw version */
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						    FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_VERSION_ADDR,
						    verbuf_fw, sizeof(verbuf_fw),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read EEPROM firmware version: ");
		return FALSE;
	}
	version_fw = g_strdup_printf ("%02X.%02X.%02X.%02X",
				      verbuf_fw[1], verbuf_fw[0],
				      verbuf_fw[3], verbuf_fw[2]);
	fu_device_set_version_bootloader (device, version_fw);

	/* use a different address if a patch is in use */
	if (self->eeprom_patch_valid_addr != 0x0) {
		if (!fu_synaptics_cxaudio_device_ensure_patch_level (self, error))
			return FALSE;
	}
	addr = self->patch_level == 0 ? FU_SYNAPTICS_CXAUDIO_EEPROM_CPX_PATCH_VERSION_ADDRESS :
					FU_SYNAPTICS_CXAUDIO_EEPROM_CPX_PATCH2_VERSION_ADDRESS;
	if (!fu_synaptics_cxaudio_device_operation (self,
						    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						    addr, verbuf_patch, sizeof(verbuf_patch),
						    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to read EEPROM patch version: ");
		return FALSE;
	}
	version_patch = g_strdup_printf ("%02X-%02X-%02X",
					 verbuf_patch[0], verbuf_patch[1], verbuf_patch[2]);
	fu_device_set_version (device, version_patch, FWUPD_VERSION_FORMAT_PLAIN);

	/* find out if patch supports additional capabilities (optional) */
	cap_str = g_usb_device_get_string_descriptor (usb_device,
						      FU_SYNAPTICS_CXAUDIO_DEVICE_CAPABILITIES_STRIDX,
						      NULL);
	if (cap_str != NULL) {
		g_auto(GStrv) split = g_strsplit (cap_str, ";", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_debug ("capability: %s", split[i]);
			if (g_strcmp0 (split[i], "RESET") == 0)
				self->sw_reset_supported = TRUE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, FU_SYNAPTICS_CXAUDIO_HID_INTERFACE,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_synaptics_cxaudio_device_prepare_firmware (FuDevice *device,
					      GBytes *fw,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE (device);
	guint32 chip_id_base;
	g_autoptr(FuFirmware) firmware = fu_synaptics_cxaudio_firmware_new ();

	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	chip_id_base = fu_synaptics_cxaudio_firmware_get_devtype (FU_SYNAPTICS_CXAUDIO_FIRMWARE (firmware));
	if (chip_id_base != self->chip_id_base) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "device 0x%04u is incompatible with firmware 0x%04u",
			     self->chip_id_base, chip_id_base);
		return NULL;
	}
	return g_steal_pointer (&firmware);
}

static gboolean
fu_synaptics_cxaudio_device_write_firmware (FuDevice *device,
					    FuFirmware *firmware,
					    FwupdInstallFlags flags,
					    GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE (device);
	GPtrArray *records = fu_srec_firmware_get_records (FU_SREC_FIRMWARE (firmware));
	FuSynapticsCxaudioFileKind file_kind;

	/* check if a patch file fits completely into the EEPROM */
	for (guint i = 0; i < records->len; i++) {
		FuSrecFirmwareRecord *rcd = g_ptr_array_index (records, i);
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S9_TERMINATION_16)
			continue;
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_LAST)
			continue;
		if (rcd->addr > self->eeprom_sz) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "EEPROM address 0x%02x is bigger than size 0x%02x",
				     rcd->addr, self->eeprom_sz);
			return FALSE;
		}
	}

	/* park the FW: run only the basic functionality until the upgrade is over */
	if (!fu_synaptics_cxaudio_device_register_set_bit (self, FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_PARK_ADDR, 7, error))
		return FALSE;
	g_usleep (10 * 1000);

	/* initialize layout signature and version to 0 if transitioning from
	 * EEPROM layout version 1 => 0 */
	file_kind = fu_synaptics_cxaudio_firmware_get_file_type (FU_SYNAPTICS_CXAUDIO_FIRMWARE (firmware));
	if (file_kind == FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2070X_FW &&
	    self->eeprom_layout_version >= 1 &&
	    fu_synaptics_cxaudio_firmware_get_layout_version (FU_SYNAPTICS_CXAUDIO_FIRMWARE (firmware)) == 0) {
		guint8 value = 0;
		if (!fu_synaptics_cxaudio_device_operation (self,
							    FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
							    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
							    FU_SYNAPTICS_CXAUDIO_EEPROM_LAYOUT_SIGNATURE_ADDRESS,
							    &value, sizeof(value),
							    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
							    error)) {
			g_prefix_error (error, "failed to initialize layout signature ");
			return FALSE;
		}
		if (!fu_synaptics_cxaudio_device_operation (self,
							    FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
							    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
							    FU_SYNAPTICS_CXAUDIO_EEPROM_LAYOUT_VERSION_ADDRESS,
							    &value, sizeof(value),
							    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
							    error)) {
			g_prefix_error (error, "failed to initialize layout signature ");
			return FALSE;
		}
		g_debug ("initialized layout signature");
	}

	/* perform the actual write */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < records->len; i++) {
		FuSrecFirmwareRecord *rcd = g_ptr_array_index (records, i);
		if (rcd->kind != FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32)
			continue;
		g_debug ("writing @0x%04x len:0x%02x", rcd->addr, rcd->buf->len);
		if (!fu_synaptics_cxaudio_device_operation (self,
							    FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
							    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
							    rcd->addr,
							    rcd->buf->data, rcd->buf->len,
							    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY,
							    error)) {
			g_prefix_error (error, "failed to write @0x%04x len:0x%02x: ",
					rcd->addr, rcd->buf->len);
			return FALSE;
		}
		fu_device_set_progress_full (device, (gsize) i, (gsize) records->len);
	}

	/* in case of a full FW upgrade invalidate the old FW patch (if any)
	 * as it may have not been done by the S37 file */
	if (file_kind == FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2070X_FW) {
		FuSynapticsCxaudioEepromPatchInfo pinfo = { 0 };
		if (!fu_synaptics_cxaudio_device_operation (self,
							    FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
							    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
							    FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_OFFSET,
							    (guint8 *) &pinfo, sizeof(pinfo),
							    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
							    error)) {
			g_prefix_error (error, "failed to read EEPROM patch info: ");
			return FALSE;
		}
		if (pinfo.PatchSignature == FU_SYNAPTICS_CXAUDIO_SIGNATURE_PATCH_BYTE) {
			memset (&pinfo, 0x0, sizeof(pinfo));
			if (!fu_synaptics_cxaudio_device_operation (self,
								    FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
								    FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
								    FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_OFFSET,
								    (guint8 *) &pinfo, sizeof(pinfo),
								    FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
								    error)) {
				g_prefix_error (error, "failed to write empty EEPROM patch info");
				return FALSE;
			}
			g_debug ("invalidated old FW patch for CX2070x (RAM) device");
		}
	}

	/* unpark the FW */
	if (!fu_synaptics_cxaudio_device_register_clear_bit (self,
							     FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_PARK_ADDR,
							     7, error))
		return FALSE;

	/* if supported, self reset */
	if (self->sw_reset_supported) {
		fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
		return fu_synaptics_cxaudio_device_reset (self, error);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_set_quirk_kv (FuDevice *device,
					const gchar *key,
					const gchar *value,
					GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE (device);
	if (g_strcmp0 (key, "ChipIdBase") == 0) {
		self->chip_id_base = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "IsSoftwareResetSupported") == 0) {
		self->sw_reset_supported = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "EepromPatchValidAddr") == 0) {
		self->eeprom_patch_valid_addr = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "EepromPatch2ValidAddr") == 0) {
		self->eeprom_patch2_valid_addr = fu_common_strtoull (value);
		return TRUE;
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_synaptics_cxaudio_device_init (FuSynapticsCxaudioDevice *self)
{
	self->sw_reset_supported = TRUE;
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration (FU_DEVICE (self), 3); /* seconds */
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.cxaudio");
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_synaptics_cxaudio_device_class_init (FuSynapticsCxaudioDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->to_string = fu_synaptics_cxaudio_device_to_string;
	klass_device->set_quirk_kv = fu_synaptics_cxaudio_device_set_quirk_kv;
	klass_device->setup = fu_synaptics_cxaudio_device_setup;
	klass_device->write_firmware = fu_synaptics_cxaudio_device_write_firmware;
	klass_device->prepare_firmware = fu_synaptics_cxaudio_device_prepare_firmware;
	klass_usb_device->open = fu_synaptics_cxaudio_device_open;
	klass_usb_device->close = fu_synaptics_cxaudio_device_close;
}
