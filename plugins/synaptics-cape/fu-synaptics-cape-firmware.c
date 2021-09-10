/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-synaptics-cape-firmware.h"

typedef struct __attribute__((packed)) {
	guint32 data[8];
} FuCapeHidFwCmdUpdateWritePar;

struct _FuSynapticsCapeFirmware {
	FuSrecFirmwareClass parent_instance;
	gchar *version;
	guint16 version_w;
	guint16 version_x;
	guint16 version_y;
	guint16 version_z;
	guint16 vid;
	guint16 pid;
};

G_DEFINE_TYPE(FuSynapticsCapeFirmware, fu_synaptics_cape_firmware, FU_TYPE_FIRMWARE)

guint16
fu_synaptics_cape_firmware_get_vid(FuSynapticsCapeFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), 0);
	return self->vid;
}

guint16
fu_synaptics_cape_firmware_get_pid(FuSynapticsCapeFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), 0);
	return self->pid;
}

static void
fu_synaptics_cape_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kv(bn, "version", self->version);
	fu_xmlb_builder_insert_kx(bn, "vid", self->vid);
	fu_xmlb_builder_insert_kx(bn, "pid", self->pid);
}

static gboolean
fu_synaptics_cape_firmware_parse_header(FuSynapticsCapeFirmware *self,
					FuFirmware *firmware,
					GBytes *fw,
					GError **error)
{
	gsize bufsz = 0x0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();

	/*the input fw image size shoud be the same as header size */
	if (bufsz < sizeof(FuCapeHidFileHeader)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "not enough data to parse header");
		return FALSE;
	}

	if (!fu_common_read_uint16_safe(buf,
					bufsz,
					FW_CAPE_HID_HEADER_OFFSET_VID,
					&self->vid,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;

	if (!fu_common_read_uint16_safe(buf,
					bufsz,
					FW_CAPE_HID_HEADER_OFFSET_PID,
					&self->pid,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;

	if (!fu_common_read_uint16_safe(buf,
					bufsz,
					FW_CAPE_HID_HEADER_OFFSET_VER_X,
					&self->version_x,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;

	if (!fu_common_read_uint16_safe(buf,
					bufsz,
					FW_CAPE_HID_HEADER_OFFSET_VER_Y,
					&self->version_y,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;

	if (!fu_common_read_uint16_safe(buf,
					bufsz,
					FW_CAPE_HID_HEADER_OFFSET_VER_Z,
					&self->version_z,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;

	self->version = g_strdup_printf("%u.%u.%u.%u",
					self->version_z,
					self->version_y,
					self->version_x,
					self->version_w);
	fu_firmware_set_version(FU_FIRMWARE(self), self->version);

	fw_hdr = fu_common_bytes_new_offset(fw, 0, sizeof(FuCapeHidFwCmdUpdateStartPar), error);
	if (fw_hdr == NULL)
		return FALSE;

	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_bytes(img_hdr, fw_hdr);
	fu_firmware_add_image(firmware, img_hdr);

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_cape_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 guint64 addr_start,
				 guint64 addr_end,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	const gsize bufsz = g_bytes_get_size(fw);
	g_autoptr(GBytes) fw_header = NULL;
	g_autoptr(GBytes) fw_body = NULL;
	const gsize headsz = sizeof(FuCapeHidFileHeader);

	/* check minimum size */
	if (bufsz < sizeof(FuCapeHidFileHeader)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "not enough data to parse header, size ");
		return FALSE;
	}

	if ((guint32)bufsz % 4 != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "data not aligned to 32 bits");
		return FALSE;
	}

	fw_header = g_bytes_new_from_bytes(fw, 0x0, headsz);

	if (fw_header == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "out of memory");
		return FALSE;
	}

	if (!fu_synaptics_cape_firmware_parse_header(self, firmware, fw_header, error))
		return FALSE;

	fw_body = g_bytes_new_from_bytes(fw, headsz, bufsz - headsz);

	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_bytes(firmware, fw_body);
	return TRUE;
}

static void
fu_synaptics_cape_firmware_init(FuSynapticsCapeFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_synaptics_cape_firmware_finalize(GObject *object)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(object);

	g_free(self->version);
	G_OBJECT_CLASS(fu_synaptics_cape_firmware_parent_class)->finalize(object);
}

static void
fu_synaptics_cape_firmware_class_init(FuSynapticsCapeFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_synaptics_cape_firmware_finalize;
	klass_firmware->parse = fu_synaptics_cape_firmware_parse;
	klass_firmware->export = fu_synaptics_cape_firmware_export;
}

FuFirmware *
fu_synaptics_cape_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPTICS_CAPE_FIRMWARE, NULL));
}
