/*
 * Copyright (C) 2017 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-pd-common.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-struct.h"

struct _FuVliPdFirmware {
	FuFirmwareClass parent_instance;
	FuVliDeviceKind device_kind;
	guint16 vid;
	guint16 pid;
};

G_DEFINE_TYPE(FuVliPdFirmware, fu_vli_pd_firmware, FU_TYPE_FIRMWARE)

FuVliDeviceKind
fu_vli_pd_firmware_get_kind(FuVliPdFirmware *self)
{
	g_return_val_if_fail(FU_IS_VLI_PD_FIRMWARE(self), 0);
	return self->device_kind;
}

static gboolean
fu_vli_pd_firmware_validate_header(FuVliPdFirmware *self)
{
	if (self->vid == 0x2109)
		return TRUE;
	if (self->vid == 0x17EF)
		return TRUE;
	if (self->vid == 0x2D01)
		return TRUE;
	if (self->vid == 0x06C4)
		return TRUE;
	if (self->vid == 0x0BF8)
		return TRUE;
	if (self->vid == 0x208E)
		return TRUE;
	return FALSE;
}

static void
fu_vli_pd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuVliPdFirmware *self = FU_VLI_PD_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn,
				  "device_kind",
				  fu_vli_device_kind_to_string(self->device_kind));
	fu_xmlb_builder_insert_kx(bn, "vid", self->vid);
	fu_xmlb_builder_insert_kx(bn, "pid", self->pid);
}

static gboolean
fu_vli_pd_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuVliPdFirmware *self = FU_VLI_PD_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint32 fwver;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *fwver_str = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_vli_pd_hdr_parse(buf, bufsz, VLI_USBHUB_PD_FLASHMAP_ADDR, error);
	if (st == NULL) {
		g_prefix_error(error, "failed to read header: ");
		return FALSE;
	}
	self->vid = fu_struct_vli_pd_hdr_get_vid(st);

	/* fall back to legacy location */
	if (!fu_vli_pd_firmware_validate_header(self)) {
		g_byte_array_unref(st);
		st = fu_struct_vli_pd_hdr_parse(buf,
						bufsz,
						VLI_USBHUB_PD_FLASHMAP_ADDR_LEGACY,
						error);
		if (st == NULL) {
			g_prefix_error(error, "failed to read header: ");
			return FALSE;
		}
	}
	self->vid = fu_struct_vli_pd_hdr_get_vid(st);

	/* urgh, not found */
	if (!fu_vli_pd_firmware_validate_header(self)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "header invalid, VID not supported");
		return FALSE;
	}

	/* guess device kind from fwver */
	fwver = fu_struct_vli_pd_hdr_get_fwver(st);
	self->device_kind = fu_vli_pd_common_guess_device_kind(fwver);
	if (self->device_kind == FU_VLI_DEVICE_KIND_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "version invalid, using 0x%x",
			    fwver);
		return FALSE;
	}
	fwver_str = fu_version_from_uint32(fwver, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_version(firmware, fwver_str);
	fu_firmware_set_version_raw(firmware, fwver);

	/* check size */
	if (bufsz != fu_vli_common_device_kind_get_size(self->device_kind)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "size invalid, got 0x%x expected 0x%x",
			    (guint)bufsz,
			    fu_vli_common_device_kind_get_size(self->device_kind));
		return FALSE;
	}

	/* check CRC */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint16 crc_actual;
		guint16 crc_file = 0x0;
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    bufsz - 2,
					    &crc_file,
					    G_LITTLE_ENDIAN,
					    error)) {
			g_prefix_error(error, "failed to read file CRC: ");
			return FALSE;
		}
		crc_actual = fu_crc16(buf, bufsz - 2);
		if (crc_actual != crc_file) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "CRC invalid, got 0x%x expected 0x%x",
				    crc_file,
				    crc_actual);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_vli_pd_firmware_init(FuVliPdFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_vli_pd_firmware_class_init(FuVliPdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_vli_pd_firmware_parse;
	klass_firmware->export = fu_vli_pd_firmware_export;
}

FuFirmware *
fu_vli_pd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_VLI_PD_FIRMWARE, NULL));
}
