/*
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
	FuFirmware parent_instance;
	guint16 vid;
	guint16 pid;
};

/* firmware update command structure, little endian */
typedef struct __attribute__((packed)) {
	guint32 vid;		/* USB vendor id */
	guint32 pid;		/* USB product id */
	guint32 fw_update_type; /* firmware update type */
	guint32 fw_signature;	/* firmware identifier */
	guint32 crc_value;	/* used to detect accidental changes to fw data */
} FuCapeHidFwCmdUpdateStartPar;

typedef struct __attribute__((packed)) {
	FuCapeHidFwCmdUpdateStartPar par;
	guint16 version_w; /* firmware version is four parts number "z.y.x.w", this is last part */
	guint16 version_x; /* firmware version, third part */
	guint16 version_y; /* firmware version, second part */
	guint16 version_z; /* firmware version, first part */
	guint32 reserved3;
} FuCapeHidFileHeader;

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
	guint16 version_w = 0;
	guint16 version_x = 0;
	guint16 version_y = 0;
	guint16 version_z = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GBytes) fw_hdr = NULL;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(firmware != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* the input fw image size should be the same as header size */
	if (bufsz < sizeof(FuCapeHidFileHeader)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "not enough data to parse header");
		return FALSE;
	}

	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_HID_HEADER_OFFSET_VID,
				    &self->vid,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_HID_HEADER_OFFSET_PID,
				    &self->pid,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_HID_HEADER_OFFSET_VER_W,
				    &version_w,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_HID_HEADER_OFFSET_VER_X,
				    &version_x,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_HID_HEADER_OFFSET_VER_Y,
				    &version_y,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_HID_HEADER_OFFSET_VER_Z,
				    &version_z,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	version_str = g_strdup_printf("%u.%u.%u.%u", version_z, version_y, version_x, version_w);
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);

	fw_hdr = fu_bytes_new_offset(fw, 0, sizeof(FuCapeHidFwCmdUpdateStartPar), error);
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
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	const gsize bufsz = g_bytes_get_size(fw);
	const gsize headsz = sizeof(FuCapeHidFileHeader);
	g_autoptr(GBytes) fw_header = NULL;
	g_autoptr(GBytes) fw_body = NULL;

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
	if (!fu_synaptics_cape_firmware_parse_header(self, firmware, fw_header, error))
		return FALSE;

	fw_body = g_bytes_new_from_bytes(fw, headsz, bufsz - headsz);
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_bytes(firmware, fw_body);
	return TRUE;
}

static GBytes *
fu_synaptics_cape_firmware_write(FuFirmware *firmware, GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	guint64 ver = fu_firmware_get_version_raw(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) payload = NULL;

	/* header */
	fu_byte_array_append_uint32(buf, self->vid, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, self->pid, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);	      /* update type */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);	      /* identifier */
	fu_byte_array_append_uint32(buf, 0xffff, G_LITTLE_ENDIAN);    /* crc_value */
	fu_byte_array_append_uint16(buf, ver >> 0, G_LITTLE_ENDIAN);  /* version w */
	fu_byte_array_append_uint16(buf, ver >> 16, G_LITTLE_ENDIAN); /* version x */
	fu_byte_array_append_uint16(buf, ver >> 32, G_LITTLE_ENDIAN); /* version y */
	fu_byte_array_append_uint16(buf, ver >> 48, G_LITTLE_ENDIAN); /* version z */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);	      /* reserved */

	/* payload */
	payload = fu_firmware_get_bytes_with_patches(firmware, error);
	if (payload == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, payload);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_32, 0xFF);

	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_synaptics_cape_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "vid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->vid = tmp;
	tmp = xb_node_query_text_as_uint(n, "pid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->pid = tmp;

	/* success */
	return TRUE;
}

static void
fu_synaptics_cape_firmware_init(FuSynapticsCapeFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_synaptics_cape_firmware_class_init(FuSynapticsCapeFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_synaptics_cape_firmware_parse;
	klass_firmware->export = fu_synaptics_cape_firmware_export;
	klass_firmware->write = fu_synaptics_cape_firmware_write;
	klass_firmware->build = fu_synaptics_cape_firmware_build;
}

FuFirmware *
fu_synaptics_cape_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPTICS_CAPE_FIRMWARE, NULL));
}
