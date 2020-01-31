/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-pd-common.h"
#include "fu-vli-pd-firmware.h"

struct _FuVliPdFirmware {
	FuFirmwareClass		 parent_instance;
	FuVliDeviceKind		 device_kind;
	FuVliPdHdr		 hdr;
	GArray			*offsets;
};

G_DEFINE_TYPE (FuVliPdFirmware, fu_vli_pd_firmware, FU_TYPE_FIRMWARE)

FuVliDeviceKind
fu_vli_pd_firmware_get_kind (FuVliPdFirmware *self)
{
	g_return_val_if_fail (FU_IS_VLI_PD_FIRMWARE (self), 0);
	return self->device_kind;
}

guint16
fu_vli_pd_firmware_get_vid (FuVliPdFirmware *self)
{
	g_return_val_if_fail (FU_IS_VLI_PD_FIRMWARE (self), 0);
	return GUINT16_FROM_LE (self->hdr.vid);
}

guint16
fu_vli_pd_firmware_get_pid (FuVliPdFirmware *self)
{
	g_return_val_if_fail (FU_IS_VLI_PD_FIRMWARE (self), 0);
	return GUINT16_FROM_LE (self->hdr.pid);
}

static void
fu_vli_pd_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuVliPdFirmware *self = FU_VLI_PD_FIRMWARE (firmware);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (self->device_kind));
	fu_common_string_append_kx (str, idt, "VID",
				    fu_vli_pd_firmware_get_vid (self));
	fu_common_string_append_kx (str, idt, "PID",
				    fu_vli_pd_firmware_get_pid (self));
}

void
fu_vli_pd_firmware_add_offset (FuVliPdFirmware *self, gsize offset)
{
	g_array_append_val (self->offsets, offset);
}

static gboolean
fu_vli_pd_firmware_parse (FuFirmware *firmware,
			  GBytes *fw,
			  guint64 addr_start,
			  guint64 addr_end,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuVliPdFirmware *self = FU_VLI_PD_FIRMWARE (firmware);
	gsize bufsz = 0;
	guint32 fwver;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autofree gchar *fwver_str = NULL;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* map into header */
	if (self->offsets->len == 0) {
		if (!fu_memcpy_safe ((guint8 *) &self->hdr, sizeof(self->hdr), 0x0,
				     buf, bufsz, 0x0,
				     sizeof(self->hdr), error)) {
			g_prefix_error (error, "failed to read header: ");
			return FALSE;
		}
	} else {
		for (guint i = 0; i < self->offsets->len; i++) {
			gsize offset = g_array_index (self->offsets, gsize, i);
			if (!fu_memcpy_safe ((guint8 *) &self->hdr, sizeof(self->hdr), 0x0,
					     buf, bufsz, offset,
					     sizeof(self->hdr), error)) {
				g_prefix_error (error, "failed to read header @0x%x: ", (guint) offset);
				return FALSE;
			}
			if (GUINT16_FROM_LE (self->hdr.vid) == 0x2109)
				break;
		}
	}

	/* guess device kind from fwver */
	fwver = GUINT32_FROM_BE (self->hdr.fwver);
	self->device_kind = fu_vli_pd_common_guess_device_kind (fwver);
	if (self->device_kind == FU_VLI_DEVICE_KIND_UNKNOWN) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "version invalid, using 0x%x", fwver);
		return FALSE;
	}
	fwver_str = fu_common_version_from_uint32 (fwver, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_set_version (firmware, fwver_str);

	/* check size */
	if (bufsz != fu_vli_common_device_kind_get_size (self->device_kind)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "size invalid, got 0x%x expected 0x%x",
			     (guint) bufsz,
			     fu_vli_common_device_kind_get_size (self->device_kind));
		return FALSE;
	}

	/* check CRC */
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		guint16 crc_actual;
		guint16 crc_file = 0x0;
		if (!fu_common_read_uint16_safe	(buf, bufsz, bufsz - 2, &crc_file,
						 G_LITTLE_ENDIAN, error)) {
			g_prefix_error (error, "failed to read file CRC: ");
			return FALSE;
		}
		crc_actual = fu_vli_common_crc16 (buf, bufsz - 2);
		if (crc_actual != crc_file) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "CRC invalid, got 0x%x expected 0x%x",
				     crc_file, crc_actual);
			return FALSE;
		}
	}

	/* whole image */
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_vli_pd_firmware_init (FuVliPdFirmware *self)
{
	self->offsets = g_array_new (FALSE, FALSE, sizeof(gsize));
}

static void
fu_vli_pd_firmware_finalize (GObject *object)
{
	FuVliPdFirmware *self = FU_VLI_PD_FIRMWARE (object);
	g_array_unref (self->offsets);
	G_OBJECT_CLASS (fu_vli_pd_firmware_parent_class)->finalize (object);
}

static void
fu_vli_pd_firmware_class_init (FuVliPdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	klass_firmware->parse = fu_vli_pd_firmware_parse;
	klass_firmware->to_string = fu_vli_pd_firmware_to_string;
	object_class->finalize = fu_vli_pd_firmware_finalize;
}

FuFirmware *
fu_vli_pd_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_VLI_PD_FIRMWARE, NULL));
}
