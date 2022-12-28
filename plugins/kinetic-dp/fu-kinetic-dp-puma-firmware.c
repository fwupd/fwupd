/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
fu_kinetic_dp_puma_firmware_parse_chip_id(GBytes *fw, FuKineticDpChip *chip_id, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	const struct {
		FuKineticDpChip chip_id;
		guint32 offset;
		const gchar *app_id;
	} map[] = {
	    {FU_KINETIC_DP_CHIP_PUMA_2900, 0x080042UL, "PUMA"} /* Puma 512KB */
	};
	for (guint32 i = 0; i < G_N_ELEMENTS(map); i++) {
		if (fu_memcmp_safe(buf,
				   bufsz,
				   map[i].offset,
				   (const guint8 *)map[i].app_id,
				   APP_ID_STR_LEN,
				   0x0,
				   APP_ID_STR_LEN,
				   NULL)) {
			*chip_id = map[i].chip_id;
			return TRUE;
		}
	}

	/* failed */
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no valid Chip ID is found in the firmware");
	return FALSE;
}

static gboolean
fu_kinetic_dp_puma_device_parse_app_fw(FuKineticDpPumaFirmware *self, GBytes *fw, GError **error)
{
	FuKineticDpPumaFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize bufsz = 0;
	gsize offset = 0;
	guint32 checksum = 0;
	guint32 code_size = 0;
	guint32 std_fw_ver = 0;
	guint8 checksum_actual;
	guint8 cmdb_sig[FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_SIG_SIZE] = {'P', 'M', 'D', 'B'};
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st = NULL;

	/* sanity check */
	if (bufsz < 512 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "firmware payload size (0x%x) is not valid",
			    (guint)bufsz);
		return FALSE;
	}

	/* calculate code size */
	st = fu_struct_kinetic_dp_puma_header_parse_bytes(fw, 0x0, error);
	if (st == NULL)
		return FALSE;
	offset += st->len;
	code_size += FU_STRUCT_KINETIC_DP_PUMA_HEADER_SIZE;
	for (guint i = 0; i < FU_STRUCT_KINETIC_DP_PUMA_HEADER_DEFAULT_OBJECT_COUNT; i++) {
		g_autoptr(GByteArray) st_obj =
		    fu_struct_kinetic_dp_puma_header_info_parse_bytes(fw, offset, error);
		if (st_obj == NULL)
			return FALSE;
		code_size += fu_struct_kinetic_dp_puma_header_info_get_length(st_obj) +
			     FU_STRUCT_KINETIC_DP_PUMA_HEADER_INFO_SIZE;
		offset += st_obj->len;
	}
	if (code_size < (512 * 1024) + offset) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid firmware -- file size 0x%x is not reasonable",
			    code_size);
		return FALSE;
	}

	/* get STD F/W version */
	std_fw_ver =
	    (guint32)(buf[FU_KINETIC_DP_PUMA_REQUEST_FW_STD_VER_START_ADDR] << 8); /* minor */
	std_fw_ver +=
	    (guint32)(buf[FU_KINETIC_DP_PUMA_REQUEST_FW_STD_VER_START_ADDR + 1] << 16); /* major */
	std_fw_ver +=
	    (guint32)(buf[FU_KINETIC_DP_PUMA_REQUEST_FW_STD_VER_START_ADDR + 2]); /* rev */
	fu_firmware_set_version_raw(FU_FIRMWARE(self), std_fw_ver);

	/* get cmbd block info */
	if (memcmp(buf + FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_START_ADDR,
		   cmdb_sig,
		   sizeof(cmdb_sig)) != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid firmware -- cmdb block not found");
		return FALSE;
	}

	if (!fu_memread_uint24_safe(buf,
				    bufsz,
				    FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_REV_ADDR,
				    &checksum,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	checksum <<= 1;

	/* calculate crc for cmbd block */
	checksum_actual = fu_sum8(buf + FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_START_ADDR,
				  FU_KINETIC_DP_PUMA_REQUEST_CMDB_SIZE);
	if (checksum_actual == checksum) {
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    FU_KINETIC_DP_PUMA_REQUEST_FW_CMDB_STD_VER_ADDR,
					    &priv->cmdb_version,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint24_safe(buf,
					    bufsz,
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
				  GBytes *fw,
				  gsize offset,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuKineticDpPumaFirmware *self = FU_KINETIC_DP_PUMA_FIRMWARE(firmware);
	FuKineticDpPumaFirmwarePrivate *priv = GET_PRIVATE(self);
	const guint8 *buf;
	gsize bufsz;
	gsize app_fw_size;
	guint32 isp_drv_size = 0;
	g_autoptr(GBytes) isp_drv_blob = NULL;
	g_autoptr(GBytes) app_fw_blob = NULL;
	g_autoptr(FuFirmware) isp_drv_img = NULL;
	g_autoptr(FuFirmware) app_fw_img = NULL;

	/* parse firmware according to Kinetic's FW image format
	 * FW binary = 4 bytes Header(Little-Endian) + ISP driver + App FW
	 * 4 bytes Header: size of ISP driver */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_memread_uint32_safe(buf, bufsz, 0, &isp_drv_size, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* add ISP driver as a new image into firmware */
	isp_drv_blob = fu_bytes_new_offset(fw, HEADER_LEN_ISP_DRV_SIZE, isp_drv_size, error);
	if (isp_drv_blob == NULL)
		return FALSE;
	isp_drv_img = fu_firmware_new_from_bytes(isp_drv_blob);
	fu_firmware_set_idx(isp_drv_img, FU_KINETIC_DP_FIRMWARE_IDX_ISP_DRV);
	fu_firmware_add_image(firmware, isp_drv_img);

	/* add App FW as a new image into firmware */
	app_fw_size = g_bytes_get_size(fw) - HEADER_LEN_ISP_DRV_SIZE - isp_drv_size;
	app_fw_blob =
	    fu_bytes_new_offset(fw, HEADER_LEN_ISP_DRV_SIZE + isp_drv_size, app_fw_size, error);
	if (app_fw_blob == NULL)
		return FALSE;
	app_fw_img = fu_firmware_new_from_bytes(app_fw_blob);
	fu_firmware_set_idx(app_fw_img, FU_KINETIC_DP_FIRMWARE_IDX_APP_FW);
	fu_firmware_add_image(firmware, app_fw_img);

	/* figure out which chip App FW it is for */
	buf = g_bytes_get_data(app_fw_blob, &bufsz);
	if (!fu_kinetic_dp_puma_firmware_parse_chip_id(app_fw_blob, &priv->chip_id, error))
		return FALSE;
	if (!fu_kinetic_dp_puma_device_parse_app_fw(self, app_fw_blob, error)) {
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_kinetic_dp_puma_firmware_parse;
	klass_firmware->export = fu_kinetic_dp_puma_firmware_export;
}
