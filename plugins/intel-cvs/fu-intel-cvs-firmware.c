/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-intel-cvs-firmware.h"
#include "fu-intel-cvs-struct.h"

struct _FuIntelCvsFirmware {
	FuFirmware parent_instance;
	guint16 vid;
	guint16 pid;
};

G_DEFINE_TYPE(FuIntelCvsFirmware, fu_intel_cvs_firmware, FU_TYPE_FIRMWARE)

static void
fu_intel_cvs_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIntelCvsFirmware *self = FU_INTEL_CVS_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "vid", self->vid);
	fu_xmlb_builder_insert_kx(bn, "pid", self->pid);
}

static gboolean
fu_intel_cvs_firmware_validate(FuFirmware *firmware,
			       GInputStream *stream,
			       gsize offset,
			       GError **error)
{
	return fu_struct_intel_cvs_firmware_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_intel_cvs_firmware_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuIntelCvsFirmware *self = FU_INTEL_CVS_FIRMWARE(firmware);
	g_autofree gchar *version = NULL;
	guint32 checksum_new;
	g_autoptr(FuStructIntelCvsFirmwareHdr) st_hdr = NULL;
	g_autoptr(FuStructIntelCvsId) st_id = NULL;
	g_autoptr(FuStructIntelCvsFw) st_fw = NULL;

	/* parse */
	st_hdr = fu_struct_intel_cvs_firmware_hdr_parse_stream(stream, offset, error);
	if (st_hdr == NULL)
		return FALSE;

	/* verify checksum of header */
	checksum_new = fu_sum32w(st_hdr->data, st_hdr->len, G_LITTLE_ENDIAN);
	if (checksum_new != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid header checksum, got 0x%x excess",
			    checksum_new);
		return FALSE;
	}

	/* get the VID/PID */
	st_id = fu_struct_intel_cvs_firmware_hdr_get_vid_pid(st_hdr);
	self->vid = fu_struct_intel_cvs_id_get_vid(st_id);
	self->pid = fu_struct_intel_cvs_id_get_pid(st_id);

	/* quad version */
	st_fw = fu_struct_intel_cvs_firmware_hdr_get_fw_version(st_hdr);
	version = g_strdup_printf("%u.%u.%u.%u",
				  fu_struct_intel_cvs_fw_get_major(st_fw),
				  fu_struct_intel_cvs_fw_get_minor(st_fw),
				  fu_struct_intel_cvs_fw_get_hotfix(st_fw),
				  fu_struct_intel_cvs_fw_get_build(st_fw));
	fu_firmware_set_version(firmware, version);
	return TRUE;
}

guint16
fu_intel_cvs_firmware_get_vid(FuIntelCvsFirmware *self)
{
	g_return_val_if_fail(FU_IS_INTEL_CVS_FIRMWARE(self), G_MAXUINT16);
	return self->vid;
}

guint16
fu_intel_cvs_firmware_get_pid(FuIntelCvsFirmware *self)
{
	g_return_val_if_fail(FU_IS_INTEL_CVS_FIRMWARE(self), G_MAXUINT16);
	return self->pid;
}

static void
fu_intel_cvs_firmware_init(FuIntelCvsFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_intel_cvs_firmware_class_init(FuIntelCvsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_intel_cvs_firmware_validate;
	firmware_class->parse = fu_intel_cvs_firmware_parse;
	firmware_class->export = fu_intel_cvs_firmware_export;
}

FuFirmware *
fu_intel_cvs_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_INTEL_CVS_FIRMWARE, NULL));
}
