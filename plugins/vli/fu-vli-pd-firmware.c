/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-pd-common.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-struct.h"

struct _FuVliPdFirmware {
	FuFirmwareClass parent_instance;
	FuVliDeviceKind device_kind;
};

G_DEFINE_TYPE(FuVliPdFirmware, fu_vli_pd_firmware, FU_TYPE_FIRMWARE)

FuVliDeviceKind
fu_vli_pd_firmware_get_kind(FuVliPdFirmware *self)
{
	g_return_val_if_fail(FU_IS_VLI_PD_FIRMWARE(self), 0);
	return self->device_kind;
}

static void
fu_vli_pd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuVliPdFirmware *self = FU_VLI_PD_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn,
				  "device_kind",
				  fu_vli_device_kind_to_string(self->device_kind));
}

static gboolean
fu_vli_pd_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuVliPdFirmware *self = FU_VLI_PD_FIRMWARE(firmware);
	gsize streamsz = 0;
	guint32 fwver;
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_vli_pd_hdr_parse_stream(stream, VLI_USBHUB_PD_FLASHMAP_ADDR, error);
	if (st == NULL) {
		g_prefix_error(error, "failed to read header: ");
		return FALSE;
	}
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

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
	fu_firmware_set_version_raw(firmware, fwver);

	/* check size */
	if (streamsz != fu_vli_common_device_kind_get_size(self->device_kind)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "size invalid, got 0x%x expected 0x%x",
			    (guint)streamsz,
			    fu_vli_common_device_kind_get_size(self->device_kind));
		return FALSE;
	}

	/* check CRC */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint16 crc_actual = 0x0;
		guint16 crc_file = 0x0;
		g_autoptr(GInputStream) stream_tmp = NULL;

		if (streamsz < 2) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "stream was too small");
			return FALSE;
		}
		if (!fu_input_stream_read_u16(stream,
					      streamsz - 2,
					      &crc_file,
					      G_LITTLE_ENDIAN,
					      error)) {
			g_prefix_error(error, "failed to read file CRC: ");
			return FALSE;
		}
		stream_tmp = fu_partial_input_stream_new(stream, 0, streamsz - 2, error);
		if (stream_tmp == NULL)
			return FALSE;
		if (!fu_input_stream_compute_crc16(stream_tmp,
						   FU_CRC_KIND_B16_USB,
						   &crc_actual,
						   error))
			return FALSE;
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

static gchar *
fu_vli_pd_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_vli_pd_firmware_init(FuVliPdFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_vli_pd_firmware_class_init(FuVliPdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_vli_pd_firmware_convert_version;
	firmware_class->parse = fu_vli_pd_firmware_parse;
	firmware_class->export = fu_vli_pd_firmware_export;
}

FuFirmware *
fu_vli_pd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_VLI_PD_FIRMWARE, NULL));
}
