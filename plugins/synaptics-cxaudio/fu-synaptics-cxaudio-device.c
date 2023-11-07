/*
 * Copyright (C) 2005 Synaptics Incorporated
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-synaptics-cxaudio-common.h"
#include "fu-synaptics-cxaudio-device.h"
#include "fu-synaptics-cxaudio-firmware.h"
#include "fu-synaptics-cxaudio-struct.h"

struct _FuSynapticsCxaudioDevice {
	FuHidDevice parent_instance;
	guint32 chip_id_base;
	guint32 chip_id;
	gboolean serial_number_set;
	gboolean sw_reset_supported;
	guint32 eeprom_layout_version;
	guint32 eeprom_patch2_valid_addr;
	guint32 eeprom_patch_valid_addr;
	guint32 eeprom_storage_address;
	guint32 eeprom_storage_sz;
	guint32 eeprom_sz;
	guint8 patch_level;
};

G_DEFINE_TYPE(FuSynapticsCxaudioDevice, fu_synaptics_cxaudio_device, FU_TYPE_HID_DEVICE)

static void
fu_synaptics_cxaudio_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE(device);
	fu_string_append_ku(str, idt, "ChipIdBase", self->chip_id_base);
	fu_string_append_ku(str, idt, "ChipId", self->chip_id);
	fu_string_append_kx(str, idt, "EepromLayoutVersion", self->eeprom_layout_version);
	fu_string_append_kx(str, idt, "EepromStorageAddress", self->eeprom_storage_address);
	fu_string_append_kx(str, idt, "EepromStorageSz", self->eeprom_storage_sz);
	fu_string_append_kx(str, idt, "EepromSz", self->eeprom_sz);
	fu_string_append_kb(str, idt, "SwResetSupported", self->sw_reset_supported);
	fu_string_append_kb(str, idt, "SerialNumberSet", self->serial_number_set);
}

static gboolean
fu_synaptics_cxaudio_device_output_report(FuSynapticsCxaudioDevice *self,
					  guint8 *buf,
					  guint16 bufsz,
					  GError **error)
{
	/* weird */
	if (buf[0] == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "report 0 not supported");
		return FALSE;
	}

	/* to device */
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					buf[0],
					buf,
					bufsz,
					FU_SYNAPTICS_CXAUDIO_USB_TIMEOUT,
					FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

static gboolean
fu_synaptics_cxaudio_device_input_report(FuSynapticsCxaudioDevice *self,
					 guint8 ReportID,
					 guint8 *buf,
					 guint16 bufsz,
					 GError **error)
{
	return fu_hid_device_get_report(FU_HID_DEVICE(self),
					ReportID,
					buf,
					bufsz,
					FU_SYNAPTICS_CXAUDIO_USB_TIMEOUT,
					FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

typedef enum {
	FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
	FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
	FU_SYNAPTICS_CXAUDIO_OPERATION_LAST
} FuSynapticsCxaudioOperation;

typedef enum {
	FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE = 0,
	FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY = (1 << 4),
} FuSynapticsCxaudioOperationFlags;

static gboolean
fu_synaptics_cxaudio_device_operation(FuSynapticsCxaudioDevice *self,
				      FuSynapticsCxaudioOperation operation,
				      FuSynapticsCxaudioMemKind mem_kind,
				      guint32 addr,
				      guint8 *buf,
				      gsize bufsz,
				      FuSynapticsCxaudioOperationFlags flags,
				      GError **error)
{
	const guint32 idx_read = 0x1;
	const guint32 idx_write = 0x5;
	const guint32 payload_max = 0x20;
	guint32 size = 0x02800;
	g_autoptr(GPtrArray) chunks = NULL;

	g_return_val_if_fail(bufsz > 0, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);

	/* check if memory operation is supported by device */
	if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE &&
	    mem_kind == FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_ROM) {
		g_set_error(error,
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
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "address out of range 0x%x < 0x%x",
			    addr,
			    size);
		return FALSE;
	}

	/* send to hardware */
	chunks = fu_chunk_array_mutable_new(buf, bufsz, addr, 0x0, payload_max);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 inbuf[FU_SYNAPTICS_CXAUDIO_INPUT_REPORT_SIZE] = {0};
		guint8 outbuf[FU_SYNAPTICS_CXAUDIO_OUTPUT_REPORT_SIZE] = {0};

		/* first byte is always report ID */
		outbuf[0] = FU_SYNAPTICS_CXAUDIO_MEM_WRITEID;

		/* set memory address and payload length (if relevant) */
		if (fu_chunk_get_address(chk) >= 64 * 1024)
			outbuf[1] |= 1 << 4;
		outbuf[2] = fu_chunk_get_data_sz(chk);
		fu_memwrite_uint16(outbuf + 3, fu_chunk_get_address(chk), G_BIG_ENDIAN);

		/* set memtype */
		if (mem_kind == FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM)
			outbuf[1] |= 1 << 5;

		/* fill the report payload part */
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE) {
			outbuf[1] |= 1 << 6;
			if (!fu_memcpy_safe(outbuf,
					    sizeof(outbuf),
					    idx_write, /* dst */
					    fu_chunk_get_data(chk),
					    fu_chunk_get_data_sz(chk),
					    0x0, /* src */
					    fu_chunk_get_data_sz(chk),
					    error))
				return FALSE;
		}
		if (!fu_synaptics_cxaudio_device_output_report(self, outbuf, sizeof(outbuf), error))
			return FALSE;

		/* issue additional write directive to read */
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE &&
		    flags & FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY) {
			outbuf[1] &= ~(1 << 6);
			if (!fu_synaptics_cxaudio_device_output_report(self,
								       outbuf,
								       sizeof(outbuf),
								       error))
				return FALSE;
		}
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_READ ||
		    flags & FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY) {
			if (!fu_synaptics_cxaudio_device_input_report(
				self,
				FU_SYNAPTICS_CXAUDIO_MEM_READID,
				inbuf,
				sizeof(inbuf),
				error))
				return FALSE;
		}
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE &&
		    flags & FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY) {
			if (!fu_memcmp_safe(outbuf,
					    sizeof(outbuf),
					    idx_write,
					    inbuf,
					    sizeof(inbuf),
					    idx_read,
					    payload_max,
					    error)) {
				g_prefix_error(error,
					       "failed to verify on packet %u @0x%x: ",
					       fu_chunk_get_idx(chk),
					       fu_chunk_get_address(chk));
				return FALSE;
			}
		}
		if (operation == FU_SYNAPTICS_CXAUDIO_OPERATION_READ) {
			if (!fu_memcpy_safe(fu_chunk_get_data_out(chk),
					    fu_chunk_get_data_sz(chk),
					    0x0, /* dst */
					    inbuf,
					    sizeof(inbuf),
					    idx_read, /* src */
					    fu_chunk_get_data_sz(chk),
					    error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_register_clear_bit(FuSynapticsCxaudioDevice *self,
					       guint32 address,
					       guint8 bit_position,
					       GError **error)
{
	guint8 tmp = 0x0;
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						   address,
						   &tmp,
						   sizeof(tmp),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error))
		return FALSE;
	tmp &= ~(1 << bit_position);
	return fu_synaptics_cxaudio_device_operation(self,
						     FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
						     FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						     address,
						     &tmp,
						     sizeof(guint8),
						     FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						     error);
}

static gboolean
fu_synaptics_cxaudio_device_register_set_bit(FuSynapticsCxaudioDevice *self,
					     guint32 address,
					     guint8 bit_position,
					     GError **error)
{
	guint8 tmp = 0x0;
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						   address,
						   &tmp,
						   sizeof(tmp),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error))
		return FALSE;
	tmp |= 1 << bit_position;
	return fu_synaptics_cxaudio_device_operation(self,
						     FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
						     FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						     address,
						     &tmp,
						     sizeof(tmp),
						     FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						     error);
}

static gchar *
fu_synaptics_cxaudio_device_eeprom_read_string(FuSynapticsCxaudioDevice *self,
					       guint32 address,
					       GError **error)
{
	guint8 buf[FU_STRUCT_SYNAPTICS_CXAUDIO_STRING_HEADER_SIZE] = {0};
	guint8 header_length;
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* read header */
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						   address,
						   buf,
						   sizeof(buf),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM string header @0x%x: ", address);
		return NULL;
	}

	/* sanity check */
	st = fu_struct_synaptics_cxaudio_string_header_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return NULL;
	header_length = fu_struct_synaptics_cxaudio_string_header_get_length(st);
	if (header_length < st->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "EEPROM string header length invalid");
		return NULL;
	}

	/* allocate buffer + NUL terminator */
	str = g_malloc0(header_length - st->len + 1);
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						   address + sizeof(buf),
						   (guint8 *)str,
						   header_length - sizeof(buf),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM string @0x%x: ", address);
		return NULL;
	}
	return g_steal_pointer(&str);
}

static gboolean
fu_synaptics_cxaudio_device_ensure_patch_level(FuSynapticsCxaudioDevice *self, GError **error)
{
	guint8 tmp = 0x0;
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						   self->eeprom_patch_valid_addr,
						   &tmp,
						   sizeof(tmp),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM patch validation byte: ");
		return FALSE;
	}
	if (tmp == FU_SYNAPTICS_CXAUDIO_SIGNATURE_PATCH_BYTE) {
		self->patch_level = 1;
		return TRUE;
	}
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						   self->eeprom_patch2_valid_addr,
						   &tmp,
						   sizeof(tmp),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM patch validation byte: ");
		return FALSE;
	}
	if (tmp == FU_SYNAPTICS_CXAUDIO_SIGNATURE_PATCH_BYTE) {
		self->patch_level = 2;
		return TRUE;
	}

	/* not sure what to do here */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "EEPROM patch version undiscoverable");
	return FALSE;
}

static gboolean
fu_synaptics_cxaudio_device_setup(FuDevice *device, GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	guint32 addr = FU_SYNAPTICS_CXAUDIO_EEPROM_CPX_PATCH_VERSION_ADDRESS;
	guint8 chip_id_offset = 0x0;
	guint8 sigbuf[FU_STRUCT_SYNAPTICS_CXAUDIO_VALIDITY_SIGNATURE_SIZE] = {0x0};
	guint8 verbuf_fw[4] = {0x0};
	guint8 verbuf_patch[3] = {0x0};
	g_autofree gchar *cap_str = NULL;
	g_autofree gchar *chip_id = NULL;
	g_autofree gchar *summary = NULL;
	g_autofree gchar *version_fw = NULL;
	g_autofree gchar *version_patch = NULL;
	g_autoptr(GByteArray) st_inf = NULL;
	g_autoptr(GByteArray) st_sig = NULL;
	guint8 cinfo[FU_STRUCT_SYNAPTICS_CXAUDIO_CUSTOM_INFO_SIZE] = {0x0};

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_synaptics_cxaudio_device_parent_class)->setup(device, error))
		return FALSE;

	/* get the ChipID */
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						   0x1005,
						   &chip_id_offset,
						   sizeof(chip_id_offset),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read ChipID: ");
		return FALSE;
	}
	self->chip_id = self->chip_id_base + chip_id_offset;

	/* add instance ID */
	chip_id = g_strdup_printf("CX%u", self->chip_id);
	fu_device_add_instance_str(device, "ID", chip_id);
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "SYNAPTICS_CXAUDIO",
					      "ID",
					      NULL))
		return FALSE;

	/* set summary */
	summary = g_strdup_printf("CX%u USB audio device", self->chip_id);
	fu_device_set_summary(device, summary);

	/* read the EEPROM validity signature */
	if (!fu_synaptics_cxaudio_device_operation(
		self,
		FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
		FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
		FU_SYNAPTICS_CXAUDIO_EEPROM_VALIDITY_SIGNATURE_OFFSET,
		sigbuf,
		sizeof(sigbuf),
		FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
		error)) {
		g_prefix_error(error, "failed to read EEPROM signature bytes: ");
		return FALSE;
	}

	/* blank EEPROM */
	if (sigbuf[0] == 0xff && sigbuf[1] == 0xff) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "EEPROM is missing or blank");
		return FALSE;
	}

	/* is disabled on EVK board using jumper */
	if ((sigbuf[0] == 0x00 && sigbuf[1] == 0x00) || (sigbuf[0] == 0xff && sigbuf[1] == 0x00)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "EEPROM has been disabled using a jumper");
		return FALSE;
	}

	/* check magic byte */
	st_sig = fu_struct_synaptics_cxaudio_validity_signature_parse(sigbuf,
								      sizeof(sigbuf),
								      0x0,
								      error);
	if (st_sig == NULL)
		return FALSE;
	if (fu_struct_synaptics_cxaudio_validity_signature_get_magic_byte(st_sig) !=
	    FU_STRUCT_SYNAPTICS_CXAUDIO_VALIDITY_SIGNATURE_DEFAULT_MAGIC_BYTE) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "EEPROM magic byte invalid, got 0x%02x expected 0x%02x",
		    fu_struct_synaptics_cxaudio_validity_signature_get_magic_byte(st_sig),
		    (guint)FU_STRUCT_SYNAPTICS_CXAUDIO_VALIDITY_SIGNATURE_DEFAULT_MAGIC_BYTE);
		return FALSE;
	}

	/* calculate EEPROM size */
	self->eeprom_sz =
	    (guint32)1
	    << (fu_struct_synaptics_cxaudio_validity_signature_get_eeprom_size_code(st_sig) + 8);
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						   FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_SIZE_ADDRESS,
						   sigbuf,
						   sizeof(sigbuf),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM signature bytes: ");
		return FALSE;
	}
	self->eeprom_storage_sz = fu_memread_uint16(sigbuf, G_LITTLE_ENDIAN);
	if (self->eeprom_storage_sz <
	    self->eeprom_sz - FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_PADDING_SIZE) {
		self->eeprom_storage_address = self->eeprom_sz - self->eeprom_storage_sz -
					       FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_PADDING_SIZE;
	}

	/* get EEPROM custom info */
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						   FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET,
						   cinfo,
						   sizeof(cinfo),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM custom info: ");
		return FALSE;
	}

	/* parse */
	st_inf = fu_struct_synaptics_cxaudio_custom_info_parse(cinfo, sizeof(cinfo), 0x0, error);
	if (st_inf == NULL)
		return FALSE;
	if (fu_struct_synaptics_cxaudio_custom_info_get_layout_signature(st_inf) ==
	    FU_SYNAPTICS_CXAUDIO_SIGNATURE_BYTE)
		self->eeprom_layout_version =
		    fu_struct_synaptics_cxaudio_custom_info_get_layout_version(st_inf);

	/* serial number, which also allows us to recover it after write */
	if (self->eeprom_layout_version >= 0x01) {
		guint16 serial_number_string_address =
		    fu_struct_synaptics_cxaudio_custom_info_get_serial_number_string_address(
			st_inf);
		self->serial_number_set = serial_number_string_address != 0x0;
		if (self->serial_number_set) {
			g_autofree gchar *tmp = NULL;
			tmp = fu_synaptics_cxaudio_device_eeprom_read_string(
			    self,
			    serial_number_string_address,
			    error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_serial(device, tmp);
		}
	}

	/* read fw version */
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						   FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_VERSION_ADDR,
						   verbuf_fw,
						   sizeof(verbuf_fw),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM firmware version: ");
		return FALSE;
	}
	version_fw = g_strdup_printf("%02X.%02X.%02X.%02X",
				     verbuf_fw[1],
				     verbuf_fw[0],
				     verbuf_fw[3],
				     verbuf_fw[2]);
	fu_device_set_version_bootloader(device, version_fw);

	/* use a different address if a patch is in use */
	if (self->eeprom_patch_valid_addr != 0x0) {
		if (!fu_synaptics_cxaudio_device_ensure_patch_level(self, error))
			return FALSE;
	}
	if (self->patch_level == 2)
		addr = FU_SYNAPTICS_CXAUDIO_EEPROM_CPX_PATCH2_VERSION_ADDRESS;
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
						   addr,
						   verbuf_patch,
						   sizeof(verbuf_patch),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to read EEPROM patch version: ");
		return FALSE;
	}
	version_patch =
	    g_strdup_printf("%02X-%02X-%02X", verbuf_patch[0], verbuf_patch[1], verbuf_patch[2]);
	fu_device_set_version(device, version_patch);

	/* find out if patch supports additional capabilities (optional) */
	cap_str =
	    g_usb_device_get_string_descriptor(usb_device,
					       FU_SYNAPTICS_CXAUDIO_DEVICE_CAPABILITIES_STRIDX,
					       NULL);
	if (cap_str != NULL) {
		g_auto(GStrv) split = g_strsplit(cap_str, ";", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_debug("capability: %s", split[i]);
			if (g_strcmp0(split[i], "RESET") == 0)
				self->sw_reset_supported = TRUE;
		}
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_synaptics_cxaudio_device_prepare_firmware(FuDevice *device,
					     GBytes *fw,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE(device);
	guint32 chip_id_base;
	g_autoptr(FuFirmware) firmware = fu_synaptics_cxaudio_firmware_new();
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	chip_id_base =
	    fu_synaptics_cxaudio_firmware_get_devtype(FU_SYNAPTICS_CXAUDIO_FIRMWARE(firmware));
	if (chip_id_base != self->chip_id_base) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "device 0x%04u is incompatible with firmware 0x%04u",
			    self->chip_id_base,
			    chip_id_base);
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_cxaudio_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE(device);
	GPtrArray *records = fu_srec_firmware_get_records(FU_SREC_FIRMWARE(firmware));
	FuSynapticsCxaudioFileKind file_kind;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 3, "park");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "invalidate");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "unpark");

	/* check if a patch file fits completely into the EEPROM */
	for (guint i = 0; i < records->len; i++) {
		FuSrecFirmwareRecord *rcd = g_ptr_array_index(records, i);
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S9_TERMINATION_16)
			continue;
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_LAST)
			continue;
		if (rcd->addr > self->eeprom_sz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "EEPROM address 0x%02x is bigger than size 0x%02x",
				    rcd->addr,
				    self->eeprom_sz);
			return FALSE;
		}
	}

	/* park the FW: run only the basic functionality until the upgrade is over */
	if (!fu_synaptics_cxaudio_device_register_set_bit(
		self,
		FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_PARK_ADDR,
		7,
		error))
		return FALSE;
	fu_device_sleep(device, 10); /* ms */
	fu_progress_step_done(progress);

	/* initialize layout signature and version to 0 if transitioning from
	 * EEPROM layout version 1 => 0 */
	file_kind =
	    fu_synaptics_cxaudio_firmware_get_file_type(FU_SYNAPTICS_CXAUDIO_FIRMWARE(firmware));
	if (file_kind == FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2070X_FW &&
	    self->eeprom_layout_version >= 1 &&
	    fu_synaptics_cxaudio_firmware_get_layout_version(
		FU_SYNAPTICS_CXAUDIO_FIRMWARE(firmware)) == 0) {
		guint8 value = 0;
		if (!fu_synaptics_cxaudio_device_operation(
			self,
			FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
			FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
			FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET +
			    FU_STRUCT_SYNAPTICS_CXAUDIO_CUSTOM_INFO_OFFSET_LAYOUT_SIGNATURE,
			&value,
			sizeof(value),
			FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
			error)) {
			g_prefix_error(error, "failed to initialize layout signature: ");
			return FALSE;
		}
		if (!fu_synaptics_cxaudio_device_operation(
			self,
			FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
			FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
			FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET +
			    FU_STRUCT_SYNAPTICS_CXAUDIO_CUSTOM_INFO_OFFSET_LAYOUT_VERSION,
			&value,
			sizeof(value),
			FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
			error)) {
			g_prefix_error(error, "failed to initialize layout signature: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* perform the actual write */
	for (guint i = 0; i < records->len; i++) {
		FuSrecFirmwareRecord *rcd = g_ptr_array_index(records, i);
		if (rcd->kind != FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32)
			continue;
		g_debug("writing @0x%04x len:0x%02x", rcd->addr, rcd->buf->len);
		if (!fu_synaptics_cxaudio_device_operation(
			self,
			FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
			FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
			rcd->addr,
			rcd->buf->data,
			rcd->buf->len,
			FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_VERIFY,
			error)) {
			g_prefix_error(error,
				       "failed to write @0x%04x len:0x%02x: ",
				       rcd->addr,
				       rcd->buf->len);
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)records->len);
	}
	fu_progress_step_done(progress);

	/* in case of a full FW upgrade invalidate the old FW patch (if any)
	 * as it may have not been done by the S37 file */
	if (file_kind == FU_SYNAPTICS_CXAUDIO_FILE_KIND_CX2070X_FW) {
		guint8 buf[FU_STRUCT_SYNAPTICS_CXAUDIO_PATCH_INFO_SIZE] = {0};
		g_autoptr(GByteArray) st_pat = NULL;

		if (!fu_synaptics_cxaudio_device_operation(
			self,
			FU_SYNAPTICS_CXAUDIO_OPERATION_READ,
			FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
			FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_OFFSET,
			buf,
			sizeof(buf),
			FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
			error)) {
			g_prefix_error(error, "failed to read EEPROM patch info: ");
			return FALSE;
		}
		st_pat = fu_struct_synaptics_cxaudio_patch_info_parse(buf, sizeof(buf), 0x0, error);
		if (st_pat == NULL)
			return FALSE;
		if (fu_struct_synaptics_cxaudio_patch_info_get_patch_signature(st_pat) ==
		    FU_SYNAPTICS_CXAUDIO_SIGNATURE_PATCH_BYTE) {
			fu_struct_synaptics_cxaudio_patch_info_set_patch_signature(st_pat, 0x0);
			fu_struct_synaptics_cxaudio_patch_info_set_patch_address(st_pat, 0x0);
			if (!fu_synaptics_cxaudio_device_operation(
				self,
				FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
				FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
				FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_OFFSET,
				st_pat->data,
				st_pat->len,
				FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
				error)) {
				g_prefix_error(error, "failed to write empty EEPROM patch info: ");
				return FALSE;
			}
			g_debug("invalidated old FW patch for CX2070x (RAM) device");
		}
	}
	fu_progress_step_done(progress);

	/* unpark the FW */
	if (!fu_synaptics_cxaudio_device_register_clear_bit(
		self,
		FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_PARK_ADDR,
		7,
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE(device);
	guint8 tmp = 1 << 6;
	g_autoptr(GError) error_local = NULL;

	/* is disabled on EVK board using jumper */
	if (!self->sw_reset_supported)
		return TRUE;

	/* wait for re-enumeration */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* this fails on success */
	if (!fu_synaptics_cxaudio_device_operation(self,
						   FU_SYNAPTICS_CXAUDIO_OPERATION_WRITE,
						   FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
						   FU_SYNAPTICS_CXAUDIO_REG_RESET_ADDR,
						   &tmp,
						   sizeof(tmp),
						   FU_SYNAPTICS_CXAUDIO_OPERATION_FLAG_NONE,
						   &error_local)) {
		if (g_error_matches(error_local, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_FAILED)) {
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_cxaudio_device_set_quirk_kv(FuDevice *device,
					 const gchar *key,
					 const gchar *value,
					 GError **error)
{
	FuSynapticsCxaudioDevice *self = FU_SYNAPTICS_CXAUDIO_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "CxaudioChipIdBase") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->chip_id_base = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CxaudioSoftwareReset") == 0)
		return fu_strtobool(value, &self->sw_reset_supported, error);
	if (g_strcmp0(key, "CxaudioPatch1ValidAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->eeprom_patch_valid_addr = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CxaudioPatch2ValidAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->eeprom_patch2_valid_addr = tmp;
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_synaptics_cxaudio_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 37, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 60, "reload");
}

static void
fu_synaptics_cxaudio_device_init(FuSynapticsCxaudioDevice *self)
{
	self->sw_reset_supported = TRUE;
	fu_device_add_icon(FU_DEVICE(self), "audio-card");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_install_duration(FU_DEVICE(self), 3); /* seconds */
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.cxaudio");
	fu_device_retry_set_delay(FU_DEVICE(self), 100); /* ms */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_synaptics_cxaudio_device_class_init(FuSynapticsCxaudioDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_synaptics_cxaudio_device_to_string;
	klass_device->set_quirk_kv = fu_synaptics_cxaudio_device_set_quirk_kv;
	klass_device->setup = fu_synaptics_cxaudio_device_setup;
	klass_device->write_firmware = fu_synaptics_cxaudio_device_write_firmware;
	klass_device->attach = fu_synaptics_cxaudio_device_attach;
	klass_device->prepare_firmware = fu_synaptics_cxaudio_device_prepare_firmware;
	klass_device->set_progress = fu_synaptics_cxaudio_device_set_progress;
}
