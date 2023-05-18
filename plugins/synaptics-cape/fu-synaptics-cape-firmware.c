/*
 * Copyright (C) 2021-2023 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>


#include <fu-crc.h>
#include <string.h>

#include "fu-synaptics-cape-firmware.h"

typedef struct {
	guint32 id;
	guint32 crc;
	guint32 offset;
	guint32 size;
} FuCapeSnglFwFile;

typedef struct __attribute__((packed)) {
	guint32 data[8];
} FuCapeHidFwCmdUpdateWritePar;

struct _FuSynapticsCapeFirmware {
	FuFirmware parent_instance;
	guint16 vid;
	guint16 pid;
	guint32 fw_crc;
	gboolean legacy_fw_format; /* 'legacy' refers to the current fw file has no SNGL header. */
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

	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		if(!self->legacy_fw_format)
			fu_xmlb_builder_insert_kx(bn, "crc_seed", self->fw_crc);
		fu_xmlb_builder_insert_kx(bn, "legacy format", self->legacy_fw_format);
	}
}

static gchar *
fu_synaptics_cape_firmware_img_id_to_string(guint32 id)
{
	GString *str = NULL;
	switch (id) {
	case FW_CAPE_SNGL_IMG_TYPE_ID_HID0:
		str = g_string_new(FW_CAPE_FIRMWARE_ID_HID_0);
		break;
	case FW_CAPE_SNGL_IMG_TYPE_ID_HID1:
		str = g_string_new(FW_CAPE_FIRMWARE_ID_HID_1);
		break;
	case FW_CAPE_SNGL_IMG_TYPE_ID_HOF0:
		str = g_string_new(FW_CAPE_FIRMWARE_ID_HID_OFFER_0);
		break;
	case FW_CAPE_SNGL_IMG_TYPE_ID_HOF1:
		str = g_string_new(FW_CAPE_FIRMWARE_ID_HID_OFFER_1);
		break;
	case FW_CAPE_SNGL_IMG_TYPE_ID_SFSX:
		str = g_string_new(FW_CAPE_FIRMWARE_ID_SFS);
		break;
	case FW_CAPE_SNGL_IMG_TYPE_ID_SOFX:
		str = g_string_new(FW_CAPE_FIRMWARE_ID_SFS_OFFER);
		break;
	case FW_CAPE_SNGL_IMG_TYPE_ID_SIGN:
		str = g_string_new(FW_CAPE_FIRMWARE_ID_SFS_SIGNATURE);
		break;
	default:
		return NULL;
	}

	return g_string_free(str, FALSE);
}

static gboolean
fu_synaptics_cape_firmware_add_image(FuFirmware *firmware,
				     GBytes *fw,
				     const FuCapeSnglFwFile *file,
				     GError **error)
{
	g_autofree gchar *fw_id = NULL;
	g_autoptr(FuFirmware) img = fu_firmware_new();
	g_autoptr(GBytes) data = NULL;

	// g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(img != NULL, FALSE);
	g_return_val_if_fail(firmware != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fw_id = fu_synaptics_cape_firmware_img_id_to_string(file->id);
	if (NULL == fw_id) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Unsupported fw file id: %u",
			    file->id);
		return FALSE;
	}

	data = fu_bytes_new_offset(fw, file->offset, file->size, error);
	if (data == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to reading fw data from blob",
			    file->id);
		return FALSE;
	}
	if (g_getenv("FWUPD_SYNAPTICS_CAPE_VERBOSE") != NULL)
		g_debug("FW file id %s found: crc32:0x%08x, offset:0x%08x, size:0x%08x", file->crc, fw_id, file->offset, file->size);

	fu_firmware_set_id(img, fw_id);
	fu_firmware_set_bytes(img, data);
	fu_firmware_add_image(firmware, img);

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	return TRUE;
}

static gboolean
fu_synaptics_cape_firmware_add_all_images(FuSynapticsCapeFirmware *self,
					  FuFirmware *firmware,
					  GBytes *fw,
					  GError **error,
					  const guint8 *buf,
					  gsize bufsz)
{
	guint16 num_fw_file = 0;
	g_autoptr(GArray) fw_file_array = NULL;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(firmware != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_FW_FILE_NUM,
				    &num_fw_file,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	if (!num_fw_file) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "No FW image file found");
		return FALSE;
	}

	fw_file_array = g_array_sized_new(FALSE, FALSE, sizeof(FuCapeSnglFwFile), num_fw_file);

	g_return_val_if_fail(fw_file_array, FALSE);
	g_array_set_size(fw_file_array, num_fw_file);

	for (guint i = 0; i < fw_file_array->len; i++) {
		FuCapeSnglFwFile *file = &g_array_index(fw_file_array, FuCapeSnglFwFile, i);

		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    FW_CAPE_SNGL_HEADER_OFFSET_FW_FILE_LIST + i +
						FW_CAPE_FW_FILE_LIST_OFFSET_ID,
					    &file->id,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    FW_CAPE_SNGL_HEADER_OFFSET_FW_FILE_LIST + i +
						FW_CAPE_FW_FILE_LIST_OFFSET_CRC,
					    &file->crc,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    FW_CAPE_SNGL_HEADER_OFFSET_FW_FILE_LIST + i +
						FW_CAPE_FW_FILE_LIST_OFFSET_FILE,
					    &file->offset,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;

		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    FW_CAPE_SNGL_HEADER_OFFSET_FW_FILE_LIST + i +
						FW_CAPE_FW_FILE_LIST_OFFSET_SIZE,
					    &file->size,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}

	gboolean fw_found = FALSE;

	for (guint i = 0; i < fw_file_array->len; i++) {
		FuCapeSnglFwFile *file = &g_array_index(fw_file_array, FuCapeSnglFwFile, i);

		switch (file->id) {
		case FW_CAPE_SNGL_IMG_TYPE_ID_HID0:
		case FW_CAPE_SNGL_IMG_TYPE_ID_HID1:
		case FW_CAPE_SNGL_IMG_TYPE_ID_HOF0:
		case FW_CAPE_SNGL_IMG_TYPE_ID_HOF1:
		case FW_CAPE_SNGL_IMG_TYPE_ID_SOFX:
		case FW_CAPE_SNGL_IMG_TYPE_ID_SIGN:
		case FW_CAPE_SNGL_IMG_TYPE_ID_SFSX:
			g_debug("add FILE %08X", file->id);
			if (!fu_synaptics_cape_firmware_add_image(firmware, fw, file, error)) {
				g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "failed to add imagge ID:%08X", file->id);
				return FALSE;
			}
			break;
		default:
			/*ignore unsupported file*/
		}

		if (FW_CAPE_SNGL_IMG_TYPE_ID_SFSX == file->id) {
			fw_found = TRUE;
		}
	}

	if (!fw_found) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Cannot find fw sfs data in blob");
	}

	return fw_found;
}

static gboolean
fu_synaptics_cape_firmware_parse_header(FuSynapticsCapeFirmware *self,
					FuFirmware *firmware,
					GBytes *fw,
					GError **error)
{
	guint16 version_w = 0;
	guint16 version_x = 0;
	guint16 version_y = 0;
	guint16 version_z = 0;

	g_autofree gchar *version_str = NULL;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GBytes) fw_hdr = NULL;

	gsize bufsz;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	const gsize headsz = sizeof(FuCapeHidFileHeader);
	g_autoptr(GBytes) fw_header = NULL;
	g_autoptr(GBytes) fw_body = NULL;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(firmware != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);

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
fu_synaptics_cape_firmware_parse_hid(FuFirmware *firmware,
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
	const guint8 *buf;
	guint32 magic_id;

	/* check minimum size */
	if (bufsz < FW_CAPE_SNGL_MINIMUM_SIZE) {
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

	buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_MAGIC2,
				    &magic_id,
				    G_LITTLE_ENDIAN,
				    error)) {
	}

	fw_header = fu_bytes_new_offset(fw, 0x0, headsz, error);
	if (fw_header == NULL)
		return FALSE;
	if (!fu_synaptics_cape_firmware_parse_header(self, firmware, fw_header, error))
		return FALSE;

	fw_body = fu_bytes_new_offset(fw, headsz, bufsz - headsz, error);
	if (fw_body == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_bytes(firmware, fw_body);
	return TRUE;
}

static gboolean
fu_synaptics_cape_firmware_parse_sngl(FuFirmware *firmware,
				      GBytes *fw,
				      gsize offset,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	gsize bufsz;

	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(firmware != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);

	/* check minimum size */
	if (bufsz < FW_CAPE_SNGL_MINIMUM_SIZE) {
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

	guint32 version = 0;
	guint32 filesz = 0;
	guint32 crc_calc = 0;
	guint32 crc_value = 0;

	g_autofree gchar *version_str = NULL;

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_FILE_SIZE,
				    &filesz,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	if (filesz != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "File size is incorrect");
		return FALSE;
	}

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_FILE_CRC,
				    &crc_value,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	crc_calc = fu_crc32(g_bytes_get_data(fw, NULL) + 8, g_bytes_get_size(fw) - 8);
	if (crc_calc != crc_value) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "CRC did not match, got 0x%x, expected 0x%x",
			    crc_value,
			    crc_calc);
		return FALSE;
	}

	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_HAS_CHECKSUM);

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_FILE_CRC,
				    &version,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_VID,
				    &self->vid,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_PID,
				    &self->pid,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_FW_VERSION,
				    &version,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;

	version_str = g_strdup_printf("%hhu.%hhu.%hhu.%hhu",
				      (guint8)(version >> 24),
				      (guint8)(version >> 16),
				      (guint8)(version >> 8),
				      (guint8)version);

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_FW_CRC,
				    &self->fw_crc,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	fu_firmware_set_version(FU_FIRMWARE(self), version_str);

	if (!fu_synaptics_cape_firmware_add_all_images(self, firmware, fw, error, buf, bufsz))
		return FALSE;

	g_autoptr(GBytes) fw_sfs =
	    fu_firmware_get_image_by_id_bytes(FU_FIRMWARE(self), FW_CAPE_FIRMWARE_ID_SFS, error);

	if (fw_sfs == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Cannot find firmware file \"SFS\" in blob");
	}

	// generate new fw for header.

	//	gsize sfssz;
	//	const guint8 * fw_data = g_bytes_get_data ( img_hdr, &sfssz );
	FuCapeHidFwCmdUpdateStartPar *header = g_malloc(sizeof(FuCapeHidFwCmdUpdateStartPar));
	g_return_val_if_fail(fw != NULL, FALSE);

	header->vid = self->vid;
	header->pid = self->pid;
	header->fw_update_type = FW_CAPE_FIRMWARE_PARTITION_1;
	header->fw_signature = FW_CAPE_FIRMWARE_EFS_TYPE_SFS;
	header->crc_value = self->fw_crc+1;

	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GBytes) fw_hdr = g_bytes_new(header, sizeof(FuCapeHidFwCmdUpdateStartPar));

	if (img_hdr == NULL || fw_hdr == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "memory allocation failed");
		g_free(header);
		return FALSE;
	}
	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_bytes(img_hdr, fw_hdr);
	fu_firmware_add_image(firmware, img_hdr);
	g_free(header);

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
	gboolean result;
	guint32 magic_id;

	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), FALSE);
	g_return_val_if_fail(fw != NULL, FALSE);
	g_return_val_if_fail(firmware != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    FW_CAPE_SNGL_HEADER_OFFSET_MAGIC2,
				    &magic_id,
				    G_LITTLE_ENDIAN,
				    error)) {
	}

	if (magic_id == FW_CAPE_FW_FILE_MAGIC_ID_SNGL) {
		g_debug("FW magic id: SNGL");
		result = fu_synaptics_cape_firmware_parse_sngl(firmware, fw, offset, flags, error);
		self->legacy_fw_format = FALSE;
	} else if (magic_id == FW_CAPE_FW_FILE_MAGIC_ID_HID) {
		g_debug("FW magic id: HID, legacy fw format");
		result = fu_synaptics_cape_firmware_parse_hid(firmware, fw, offset, flags, error);
		self->legacy_fw_format = TRUE;
	} else {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "FW Magic ID not found");
		result = FALSE;
	}

	return result;
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
