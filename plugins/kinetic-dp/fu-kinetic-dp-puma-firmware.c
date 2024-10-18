/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-kinetic-dp-puma-device.h"
#include "fu-kinetic-dp-puma-firmware.h"
#include "fu-kinetic-dp-secure-device.h"

struct _FuKineticDpPumaFirmware {
	FuFirmwareClass parent_instance;
};

typedef struct {
	FuKineticDpChip chip_id;
	guint16 cmdb_version;
	guint32 cmdb_revision;
} FuKineticDpPumaFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuKineticDpPumaFirmware, fu_kinetic_dp_puma_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_kinetic_dp_puma_firmware_get_instance_private(o))

#define HEADER_LEN_ISP_DRV_SIZE 4
#define APP_ID_STR_LEN		4

#define FU_KINETIC_DP_PUMA_REQUEST_FW_HEADER_SIZE 50
#define FU_KINETIC_DP_PUMA_REQUEST_FW_HASH_SIZE	  32
#define PUMA_STS_FW_PAYLOAD_SIZE                                                                   \
	((512 * 1024) + FU_KINETIC_DP_PUMA_REQUEST_FW_HEADER_SIZE +                                \
	 (FU_KINETIC_DP_PUMA_REQUEST_FW_HASH_SIZE * 2))

/* Puma STD F/W SPI mapping */
#define FU_KINETIC_DP_PUMA_REQUEST_FW_STD_VER_START_ADDR (PUMA_STS_FW_PAYLOAD_SIZE - 52) /*0x8003E*/

/* Puma STD F/W CMDB */
#define FU_KINETIC_DP_PUMA_REQUEST_CMDB_SIZE		128
#define FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_SIG_SIZE	4
#define FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_START_ADDR	0x7FE52
#define FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_STD_VER_ADDR 0x7FE56
#define FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_REV_ADDR	0x7FE58
#define FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_REV_SIZE	3

static void
fu_kinetic_dp_puma_firmware_export(FuFirmware *firmware,
				   FuFirmwareExportFlags flags,
				   XbBuilderNode *bn)
{
	FuKineticDpPumaFirmware *self = FU_KINETIC_DP_PUMA_FIRMWARE(firmware);
	FuKineticDpPumaFirmwarePrivate *priv = GET_PRIVATE(self);

	fu_xmlb_builder_insert_kv(bn, "chip_id", fu_kinetic_dp_chip_to_string(priv->chip_id));
	fu_xmlb_builder_insert_kx(bn, "cmdb_version", priv->cmdb_version);
	fu_xmlb_builder_insert_kx(bn, "cmdb_revision", priv->cmdb_revision);
}

static gboolean
fu_kinetic_dp_puma_firmware_parse_chip_id(GInputStream *stream,
					  FuKineticDpChip *chip_id,
					  GError **error)
{
	const struct {
		FuKineticDpChip chip_id;
		guint32 offset;
		const gchar *app_id;
	} map[] = {
	    {FU_KINETIC_DP_CHIP_PUMA_2900, 0x080042UL, "PUMA"} /* Puma 512KB */
	};
	for (guint32 i = 0; i < G_N_ELEMENTS(map); i++) {
		guint8 buf[4] = {APP_ID_STR_LEN};
		if (!fu_input_stream_read_safe(stream,
					       buf,
					       sizeof(buf),
					       0x0,
					       map[i].offset,
					       sizeof(buf),
					       error))
			return FALSE;
		if (memcmp(buf, (const guint8 *)map[i].app_id, sizeof(buf)) == 0) {
			*chip_id = map[i].chip_id;
			return TRUE;
		}
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no valid Chip ID is found in the firmware");
	return FALSE;
}

static gboolean
fu_kinetic_dp_puma_firmware_parse_app_fw(FuKineticDpPumaFirmware *self,
					 GInputStream *stream,
					 GError **error)
{
	FuKineticDpPumaFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize streamsz = 0;
	gsize offset = 0;
	guint32 checksum = 0;
	guint32 code_size = 0;
	guint32 std_fw_ver = 0;
	guint8 tmpbuf = 0;
	guint8 checksum_actual;
	guint8 cmdb_sig[FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_SIG_SIZE] = {'P', 'M', 'D', 'B'};
	g_autoptr(GByteArray) cmdb_tmp = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* sanity check */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < 512 * 1024) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware payload size (0x%x) is not valid",
			    (guint)streamsz);
		return FALSE;
	}

	/* calculate code size */
	st = fu_struct_kinetic_dp_puma_header_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	offset += st->len;
	code_size += FU_STRUCT_KINETIC_DP_PUMA_HEADER_SIZE;
	for (guint i = 0; i < FU_STRUCT_KINETIC_DP_PUMA_HEADER_DEFAULT_OBJECT_COUNT; i++) {
		g_autoptr(GByteArray) st_obj =
		    fu_struct_kinetic_dp_puma_header_info_parse_stream(stream, offset, error);
		if (st_obj == NULL)
			return FALSE;
		code_size += fu_struct_kinetic_dp_puma_header_info_get_length(st_obj) +
			     FU_STRUCT_KINETIC_DP_PUMA_HEADER_INFO_SIZE;
		offset += st_obj->len;
	}
	if (code_size < (512 * 1024) + offset) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid firmware -- file size 0x%x is not reasonable",
			    code_size);
		return FALSE;
	}

	/* get STD F/W version */
	if (!fu_input_stream_read_u8(stream,
				     FU_KINETIC_DP_PUMA_REQUEST_FW_STD_VER_START_ADDR,
				     &tmpbuf,
				     error))
		return FALSE;
	std_fw_ver += (guint32)tmpbuf << 8;
	if (!fu_input_stream_read_u8(stream,
				     FU_KINETIC_DP_PUMA_REQUEST_FW_STD_VER_START_ADDR + 1,
				     &tmpbuf,
				     error))
		return FALSE;
	std_fw_ver += (guint32)tmpbuf << 16;
	if (!fu_input_stream_read_u8(stream,
				     FU_KINETIC_DP_PUMA_REQUEST_FW_STD_VER_START_ADDR + 2,
				     &tmpbuf,
				     error))
		return FALSE;
	std_fw_ver += (guint32)tmpbuf;
	fu_firmware_set_version_raw(FU_FIRMWARE(self), std_fw_ver);

	/* get cmbd block info */
	cmdb_tmp = fu_input_stream_read_byte_array(stream,
						   FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_START_ADDR,
						   FU_KINETIC_DP_PUMA_REQUEST_CMDB_SIZE,
						   NULL,
						   error);
	if (cmdb_tmp == NULL)
		return FALSE;
	if (cmdb_tmp->len != FU_KINETIC_DP_PUMA_REQUEST_CMDB_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid firmware -- cmdb block invalid");
		return FALSE;
	}
	if (memcmp(cmdb_tmp->data, cmdb_sig, sizeof(cmdb_sig)) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid firmware -- cmdb block not found");
		return FALSE;
	}

	if (!fu_input_stream_read_u24(stream,
				      FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_REV_ADDR,
				      &checksum,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	checksum <<= 1;

	/* calculate crc for cmbd block */
	checksum_actual = fu_sum8(cmdb_tmp->data, cmdb_tmp->len);
	if (checksum_actual == checksum) {
		if (!fu_input_stream_read_u16(stream,
					      FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_STD_VER_ADDR,
					      &priv->cmdb_version,
					      G_BIG_ENDIAN,
					      error))
			return FALSE;
		if (!fu_input_stream_read_u24(stream,
					      FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_REV_ADDR,
					      &priv->cmdb_revision,
					      G_BIG_ENDIAN,
					      error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_kinetic_dp_puma_firmware_parse(FuFirmware *firmware,
				  GInputStream *stream,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuKineticDpPumaFirmware *self = FU_KINETIC_DP_PUMA_FIRMWARE(firmware);
	FuKineticDpPumaFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize app_fw_size;
	gsize streamsz = 0;
	guint32 isp_drv_size = 0;
	g_autoptr(GInputStream) isp_drv_stream = NULL;
	g_autoptr(GInputStream) app_fw_stream = NULL;
	g_autoptr(FuFirmware) isp_drv_img = fu_firmware_new();
	g_autoptr(FuFirmware) app_fw_img = fu_firmware_new();

	/* parse firmware according to Kinetic's FW image format
	 * FW binary = 4 bytes Header(Little-Endian) + ISP driver + App FW
	 * 4 bytes Header: size of ISP driver */
	if (!fu_input_stream_read_u32(stream, 0, &isp_drv_size, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* add ISP driver as a new image into firmware */
	isp_drv_stream =
	    fu_partial_input_stream_new(stream, HEADER_LEN_ISP_DRV_SIZE, isp_drv_size, error);
	if (isp_drv_stream == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(isp_drv_img, isp_drv_stream, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_idx(isp_drv_img, FU_KINETIC_DP_FIRMWARE_IDX_ISP_DRV);
	if (!fu_firmware_add_image_full(firmware, isp_drv_img, error))
		return FALSE;

	/* add App FW as a new image into firmware */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < HEADER_LEN_ISP_DRV_SIZE + isp_drv_size) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "stream was too small");
		return FALSE;
	}
	app_fw_size = streamsz - HEADER_LEN_ISP_DRV_SIZE - isp_drv_size;
	app_fw_stream = fu_partial_input_stream_new(stream,
						    HEADER_LEN_ISP_DRV_SIZE + isp_drv_size,
						    app_fw_size,
						    error);
	if (app_fw_stream == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(app_fw_img, app_fw_stream, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_idx(app_fw_img, FU_KINETIC_DP_FIRMWARE_IDX_APP_FW);
	if (!fu_firmware_add_image_full(firmware, app_fw_img, error))
		return FALSE;

	/* figure out which chip App FW it is for */
	if (!fu_kinetic_dp_puma_firmware_parse_chip_id(app_fw_stream, &priv->chip_id, error))
		return FALSE;
	if (!fu_kinetic_dp_puma_firmware_parse_app_fw(self, app_fw_stream, error)) {
		g_prefix_error(error, "failed to parse info from Puma App firmware: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_kinetic_dp_puma_firmware_init(FuKineticDpPumaFirmware *self)
{
}

static void
fu_kinetic_dp_puma_firmware_class_init(FuKineticDpPumaFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_kinetic_dp_puma_firmware_parse;
	firmware_class->export = fu_kinetic_dp_puma_firmware_export;
}
