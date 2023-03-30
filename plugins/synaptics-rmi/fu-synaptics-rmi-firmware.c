/*
 * Copyright (C) 2012 Andrew Duggan
 * Copyright (C) 2012 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-synaptics-rmi-common.h"
#include "fu-synaptics-rmi-firmware.h"
#include "fu-synaptics-rmi-struct.h"

typedef enum {
	RMI_FIRMWARE_KIND_UNKNOWN = 0x00,
	RMI_FIRMWARE_KIND_0X = 0x01,
	RMI_FIRMWARE_KIND_10 = 0x10,
	RMI_FIRMWARE_KIND_LAST,
} RmiFirmwareKind;

struct _FuSynapticsRmiFirmware {
	FuFirmware parent_instance;
	RmiFirmwareKind kind;
	guint32 checksum;
	guint8 io;
	guint8 bootloader_version;
	guint32 build_id;
	guint32 package_id;
	guint16 product_info;
	gchar *product_id;
	guint32 sig_size;
};

G_DEFINE_TYPE(FuSynapticsRmiFirmware, fu_synaptics_rmi_firmware, FU_TYPE_FIRMWARE)

#define RMI_IMG_FW_OFFSET		  0x100

#define RMI_IMG_V10_CNTR_ADDR_OFFSET 0x0c
#define RMI_IMG_MAX_CONTAINERS	     1024

typedef enum {
	RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL = 0,
	RMI_FIRMWARE_CONTAINER_ID_UI,
	RMI_FIRMWARE_CONTAINER_ID_UI_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_BL,
	RMI_FIRMWARE_CONTAINER_ID_BL_IMAGE,
	RMI_FIRMWARE_CONTAINER_ID_BL_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_BL_LOCKDOWN_INFO,
	RMI_FIRMWARE_CONTAINER_ID_PERMANENT_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_GUEST_CODE,
	RMI_FIRMWARE_CONTAINER_ID_BL_PROTOCOL_DESCRIPTOR,
	RMI_FIRMWARE_CONTAINER_ID_UI_PROTOCOL_DESCRIPTOR,
	RMI_FIRMWARE_CONTAINER_ID_RMI_SELF_DISCOVERY,
	RMI_FIRMWARE_CONTAINER_ID_RMI_PAGE_CONTENT,
	RMI_FIRMWARE_CONTAINER_ID_GENERAL_INFORMATION,
	RMI_FIRMWARE_CONTAINER_ID_DEVICE_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_FLASH_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_GUEST_SERIALIZATION,
	RMI_FIRMWARE_CONTAINER_ID_GLOBAL_PARAMETERS,
	RMI_FIRMWARE_CONTAINER_ID_CORE_CODE,
	RMI_FIRMWARE_CONTAINER_ID_CORE_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_DISPLAY_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_EXTERNAL_TOUCH_AFE_CONFIG,
	RMI_FIRMWARE_CONTAINER_ID_UTILITY,
	RMI_FIRMWARE_CONTAINER_ID_UTILITY_PARAMETER,
	RMI_FIRMWARE_CONTAINER_ID_FIXED_LOCATION_DATA = 27,
} RmiFirmwareContainerId;

static const gchar *
rmi_firmware_container_id_to_string(RmiFirmwareContainerId container_id)
{
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL)
		return "top-level";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_UI)
		return "ui";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_UI_CONFIG)
		return "ui-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_BL)
		return "bl";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_BL_IMAGE)
		return "bl-image";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_BL_CONFIG)
		return "bl-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_BL_LOCKDOWN_INFO)
		return "bl-lockdown-info";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_PERMANENT_CONFIG)
		return "permanent-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_GUEST_CODE)
		return "guest-code";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_BL_PROTOCOL_DESCRIPTOR)
		return "bl-protocol-descriptor";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_UI_PROTOCOL_DESCRIPTOR)
		return "ui-protocol-descriptor";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_RMI_SELF_DISCOVERY)
		return "rmi-self-discovery";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_RMI_PAGE_CONTENT)
		return "rmi-page-content";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_GENERAL_INFORMATION)
		return "general-information";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_DEVICE_CONFIG)
		return "device-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_FLASH_CONFIG)
		return "flash-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_GUEST_SERIALIZATION)
		return "guest-serialization";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_GLOBAL_PARAMETERS)
		return "global-parameters";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_CORE_CODE)
		return "core-code";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_CORE_CONFIG)
		return "core-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_DISPLAY_CONFIG)
		return "display-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_EXTERNAL_TOUCH_AFE_CONFIG)
		return "external-touch-afe-config";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_UTILITY)
		return "utility";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_UTILITY_PARAMETER)
		return "utility-parameter";
	if (container_id == RMI_FIRMWARE_CONTAINER_ID_FIXED_LOCATION_DATA)
		return "fixed-location-data";
	return NULL;
}

static gboolean
fu_synaptics_rmi_firmware_add_image(FuFirmware *firmware,
				    const gchar *id,
				    GBytes *fw,
				    gsize offset,
				    gsize bufsz,
				    GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(FuFirmware) img = NULL;

	bytes = fu_bytes_new_offset(fw, offset, bufsz, error);
	if (bytes == NULL)
		return FALSE;
	img = fu_firmware_new_from_bytes(bytes);
	fu_firmware_set_id(img, id);
	fu_firmware_add_image(firmware, img);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_firmware_add_image_v10(FuFirmware *firmware,
					const gchar *id,
					GBytes *fw,
					gsize offset,
					gsize bufsz,
					gsize sig_sz,
					GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autofree gchar *sig_id = NULL;

	if (!fu_synaptics_rmi_firmware_add_image(firmware, id, fw, offset, bufsz, error))
		return FALSE;
	if (sig_sz != 0) {
		bytes = fu_bytes_new_offset(fw, offset + bufsz, sig_sz, error);
		if (bytes == NULL)
			return FALSE;
		img = fu_firmware_new_from_bytes(bytes);
		sig_id = g_strdup_printf("%s-signature", id);
		fu_firmware_set_id(img, sig_id);
		fu_firmware_add_image(firmware, img);
	}
	return TRUE;
}

static void
fu_synaptics_rmi_firmware_export(FuFirmware *firmware,
				 FuFirmwareExportFlags flags,
				 XbBuilderNode *bn)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "kind", self->kind);
	fu_xmlb_builder_insert_kv(bn, "product_id", self->product_id);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kx(bn, "bootloader_version", self->bootloader_version);
		fu_xmlb_builder_insert_kx(bn, "io", self->io);
		fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
		fu_xmlb_builder_insert_kx(bn, "build_id", self->build_id);
		fu_xmlb_builder_insert_kx(bn, "package_id", self->package_id);
		fu_xmlb_builder_insert_kx(bn, "product_info", self->product_info);
		fu_xmlb_builder_insert_kx(bn, "sig_size", self->sig_size);
	}
}

static gboolean
fu_synaptics_rmi_firmware_parse_v10(FuFirmware *firmware, GBytes *fw, GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	guint16 container_id;
	guint32 cntrs_len;
	guint32 offset;
	guint32 cntr_addr;
	guint8 product_id[RMI_PRODUCT_ID_LENGTH] = {0x0};
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint32 signature_size;
	g_autoptr(GByteArray) st_dsc = NULL;

	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    RMI_IMG_V10_CNTR_ADDR_OFFSET,
				    &cntr_addr,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	g_debug("v10 RmiContainerDescriptor at 0x%x", cntr_addr);
	st_dsc = fu_struct_rmi_container_descriptor_parse(buf, bufsz, cntr_addr, error);
	if (st_dsc == NULL) {
		g_prefix_error(error, "RmiContainerDescriptor invalid: ");
		return FALSE;
	}
	container_id = fu_struct_rmi_container_descriptor_get_container_id(st_dsc);
	if (container_id != RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "toplevel container_id invalid, got 0x%x expected 0x%x",
			    (guint)container_id,
			    (guint)RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL);
		return FALSE;
	}
	offset = fu_struct_rmi_container_descriptor_get_content_address(st_dsc);
	if (offset > bufsz - sizeof(guint32) - st_dsc->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "image offset invalid, got 0x%x, size 0x%x",
			    (guint)offset,
			    (guint)bufsz);
		return FALSE;
	}
	cntrs_len = fu_struct_rmi_container_descriptor_get_content_length(st_dsc) / 4;
	if (cntrs_len > RMI_IMG_MAX_CONTAINERS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "too many containers in file [%u], maximum is %u",
			    cntrs_len,
			    (guint)RMI_IMG_MAX_CONTAINERS);
		return FALSE;
	}
	g_debug("offset=0x%x (cntrs_len=%u)", offset, cntrs_len);

	for (guint32 i = 0; i < cntrs_len; i++) {
		guint32 content_addr;
		guint32 addr;
		guint32 length;
		g_autoptr(GByteArray) st_dsc2 = NULL;

		if (!fu_memread_uint32_safe(buf, bufsz, offset, &addr, G_LITTLE_ENDIAN, error))
			return FALSE;
		g_debug("parsing RmiContainerDescriptor at 0x%x", addr);

		st_dsc2 = fu_struct_rmi_container_descriptor_parse(buf, bufsz, addr, error);
		if (st_dsc2 == NULL)
			return FALSE;
		container_id = fu_struct_rmi_container_descriptor_get_container_id(st_dsc2);
		content_addr = fu_struct_rmi_container_descriptor_get_content_address(st_dsc2);
		length = fu_struct_rmi_container_descriptor_get_content_length(st_dsc2);
		signature_size = fu_struct_rmi_container_descriptor_get_signature_size(st_dsc2);
		g_debug("RmiContainerDescriptor 0x%02x @ 0x%x (len 0x%x) sig_size 0x%x",
			container_id,
			content_addr,
			length,
			signature_size);
		if (length == 0 || length > bufsz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "length invalid, length 0x%x, size 0x%x",
				    (guint)length,
				    (guint)bufsz);
			return FALSE;
		}
		if (content_addr > bufsz - length) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "address invalid, got 0x%x (length 0x%x), size 0x%x",
				    (guint)content_addr,
				    (guint)length,
				    (guint)bufsz);
			return FALSE;
		}
		switch (container_id) {
		case RMI_FIRMWARE_CONTAINER_ID_BL:
			if (!fu_memread_uint8_safe(buf,
						   bufsz,
						   content_addr,
						   &self->bootloader_version,
						   error))
				return FALSE;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_UI:
		case RMI_FIRMWARE_CONTAINER_ID_CORE_CODE:
			if (!fu_synaptics_rmi_firmware_add_image_v10(firmware,
								     "ui",
								     fw,
								     content_addr,
								     length,
								     signature_size,
								     error))
				return FALSE;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_FLASH_CONFIG:
			if (!fu_synaptics_rmi_firmware_add_image_v10(firmware,
								     "flash-config",
								     fw,
								     content_addr,
								     length,
								     signature_size,
								     error))
				return FALSE;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_UI_CONFIG:
		case RMI_FIRMWARE_CONTAINER_ID_CORE_CONFIG:
			if (!fu_synaptics_rmi_firmware_add_image_v10(firmware,
								     "config",
								     fw,
								     content_addr,
								     length,
								     signature_size,
								     error))
				return FALSE;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_FIXED_LOCATION_DATA:
			if (!fu_synaptics_rmi_firmware_add_image_v10(firmware,
								     "fixed-location-data",
								     fw,
								     content_addr,
								     length,
								     signature_size,
								     error))
				return FALSE;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_EXTERNAL_TOUCH_AFE_CONFIG:
			if (!fu_synaptics_rmi_firmware_add_image_v10(firmware,
								     "afe-config",
								     fw,
								     content_addr,
								     length,
								     signature_size,
								     error))
				return FALSE;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_DISPLAY_CONFIG:
			if (!fu_synaptics_rmi_firmware_add_image_v10(firmware,
								     "display-config",
								     fw,
								     content_addr,
								     length,
								     signature_size,
								     error))
				return FALSE;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_GENERAL_INFORMATION:
			if (length < 0x18 + RMI_PRODUCT_ID_LENGTH) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "content_addr invalid, got 0x%x (length 0x%x)",
					    content_addr,
					    (guint)length);
				return FALSE;
			}
			g_clear_pointer(&self->product_id, g_free);
			self->io = 1;
			if (!fu_memread_uint32_safe(buf,
						    bufsz,
						    content_addr,
						    &self->package_id,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			if (!fu_memread_uint32_safe(buf,
						    bufsz,
						    content_addr + 0x04,
						    &self->build_id,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			if (!fu_memcpy_safe(product_id,
					    sizeof(product_id),
					    0x0, /* dst */
					    buf,
					    bufsz,
					    content_addr + 0x18, /* src */
					    sizeof(product_id),
					    error))
				return FALSE;
			break;
		default:
			g_debug("unsupported container %s [0x%02x]",
				rmi_firmware_container_id_to_string(container_id),
				container_id);
			break;
		}
		offset += 4;
	}
	if (product_id[0] != '\0') {
		g_free(self->product_id);
		self->product_id = g_strndup((const gchar *)product_id, sizeof(product_id));
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_firmware_parse_v0x(FuFirmware *firmware, GBytes *fw, GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	guint32 cfg_sz;
	guint32 img_sz;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_img = NULL;

	/* main firmware */
	st_img = fu_struct_rmi_img_parse(buf, bufsz, 0x0, error);
	if (st_img == NULL)
		return FALSE;
	img_sz = fu_struct_rmi_img_get_image_size(st_img);
	if (img_sz > 0) {
		/* payload, then signature appended */
		if (self->sig_size > 0) {
			guint32 sig_offset = img_sz - self->sig_size;
			if (!fu_synaptics_rmi_firmware_add_image(firmware,
								 "sig",
								 fw,
								 RMI_IMG_FW_OFFSET + sig_offset,
								 self->sig_size,
								 error))
				return FALSE;
		}
		if (!fu_synaptics_rmi_firmware_add_image(firmware,
							 "ui",
							 fw,
							 RMI_IMG_FW_OFFSET,
							 img_sz,
							 error))
			return FALSE;
	}

	/* config */
	cfg_sz = fu_struct_rmi_img_get_config_size(st_img);
	if (cfg_sz > 0) {
		if (!fu_synaptics_rmi_firmware_add_image(firmware,
							 "config",
							 fw,
							 RMI_IMG_FW_OFFSET + img_sz,
							 cfg_sz,
							 error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_firmware_parse(FuFirmware *firmware,
				GBytes *fw,
				gsize offset,
				FwupdInstallFlags flags,
				GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_img = NULL;

	/* sanity check */
	st_img = fu_struct_rmi_img_parse(buf, bufsz, 0x0, error);
	if (st_img == NULL)
		return FALSE;
	if (bufsz % 2 != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "data not aligned to 16 bits");
		return FALSE;
	}

	/* verify checksum */
	self->checksum = fu_struct_rmi_img_get_checksum(st_img);
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 checksum_calculated =
		    fu_synaptics_rmi_generate_checksum(buf + 4, bufsz - 4);
		if (self->checksum != checksum_calculated) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "checksum verification failed, got 0x%08x, actual 0x%08x",
				    (guint)self->checksum,
				    (guint)checksum_calculated);
			return FALSE;
		}
	}

	/* parse legacy image */
	g_clear_pointer(&self->product_id, g_free);
	self->io = fu_struct_rmi_img_get_io_offset(st_img);
	self->bootloader_version = fu_struct_rmi_img_get_bootloader_version(st_img);
	if (self->io == 1) {
		self->build_id = fu_struct_rmi_img_get_fw_build_id(st_img);
		self->package_id = fu_struct_rmi_img_get_package_id(st_img);
	}
	self->product_id = fu_struct_rmi_img_get_product_id(st_img);
	self->product_info = fu_struct_rmi_img_get_product_info(st_img);
	fu_firmware_set_size(firmware, fu_struct_rmi_img_get_image_size(st_img));

	/* parse partitions, but ignore lockdown */
	switch (self->bootloader_version) {
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
		if ((self->io & 0x10) >> 1)
			self->sig_size = fu_struct_rmi_img_get_signature_size(st_img);
		if (!fu_synaptics_rmi_firmware_parse_v0x(firmware, fw, error))
			return FALSE;
		self->kind = RMI_FIRMWARE_KIND_0X;
		break;
	case 16:
	case 17:
		if (!fu_synaptics_rmi_firmware_parse_v10(firmware, fw, error))
			return FALSE;
		self->kind = RMI_FIRMWARE_KIND_10;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unsupported image version 0x%02x",
			    self->bootloader_version);
		return FALSE;
	}

	/* success */
	return TRUE;
}

guint32
fu_synaptics_rmi_firmware_get_sig_size(FuSynapticsRmiFirmware *self)
{
	return self->sig_size;
}

static GBytes *
fu_synaptics_rmi_firmware_write_v0x(FuFirmware *firmware, GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint32 csum;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st_img = fu_struct_rmi_img_new();
	g_autoptr(GBytes) buf_blob = NULL;

	/* default image */
	img = fu_firmware_get_image_by_id(firmware, "ui", error);
	if (img == NULL)
		return NULL;
	buf_blob = fu_firmware_write(img, error);
	if (buf_blob == NULL)
		return NULL;
	bufsz = g_bytes_get_size(buf_blob);

	/* create empty block */
	fu_struct_rmi_img_set_bootloader_version(st_img, 0x2); /* not hierarchical */
	if (self->product_id != NULL) {
		if (!fu_struct_rmi_img_set_product_id(st_img, self->product_id, error))
			return NULL;
	}
	fu_struct_rmi_img_set_product_info(st_img, 0x1234);
	fu_struct_rmi_img_set_image_size(st_img, bufsz);
	fu_struct_rmi_img_set_config_size(st_img, bufsz);
	g_byte_array_append(buf, st_img->data, st_img->len);
	fu_byte_array_set_size(buf, RMI_IMG_FW_OFFSET + 0x4 + bufsz, 0x00);
	fu_memwrite_uint32(buf->data + RMI_IMG_FW_OFFSET, 0xDEAD, G_LITTLE_ENDIAN); /* img */
	fu_memwrite_uint32(buf->data + RMI_IMG_FW_OFFSET + bufsz,
			   0xBEEF,
			   G_LITTLE_ENDIAN); /* config */

	/* fixup checksum */
	csum = fu_synaptics_rmi_generate_checksum(buf->data + 4, buf->len - 4);
	fu_memwrite_uint32(buf->data + FU_STRUCT_RMI_IMG_OFFSET_CHECKSUM, csum, G_LITTLE_ENDIAN);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static GBytes *
fu_synaptics_rmi_firmware_write_v10(FuFirmware *firmware, GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	gsize bufsz;
	guint32 csum;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) desc_hdr = fu_struct_rmi_container_descriptor_new();
	g_autoptr(GByteArray) desc = fu_struct_rmi_container_descriptor_new();
	g_autoptr(GBytes) buf_blob = NULL;

	/* header | desc_hdr | offset_table | desc | flash_config |
	 *        \0x0       \0x20          \0x24  \0x44          |0x48 */
	guint32 offset_table[] = {
	    GUINT32_TO_LE(RMI_IMG_FW_OFFSET + 0x24)}; /* offset to first descriptor */
	fu_struct_rmi_container_descriptor_set_container_id(desc,
							    RMI_FIRMWARE_CONTAINER_ID_FLASH_CONFIG);
	fu_struct_rmi_container_descriptor_set_content_address(desc, RMI_IMG_FW_OFFSET + 0x44);

	/* default image */
	img = fu_firmware_get_image_by_id(firmware, "ui", error);
	if (img == NULL)
		return NULL;
	buf_blob = fu_firmware_write(img, error);
	if (buf_blob == NULL)
		return NULL;
	bufsz = g_bytes_get_size(buf_blob);
	fu_struct_rmi_container_descriptor_set_content_length(desc, bufsz);

	/* create empty block */
	fu_byte_array_set_size(buf, RMI_IMG_FW_OFFSET + 0x48, 0x00);
	buf->data[FU_STRUCT_RMI_IMG_OFFSET_IO_OFFSET] = 0x1;
	buf->data[FU_STRUCT_RMI_IMG_OFFSET_BOOTLOADER_VERSION] = 16; /* hierarchical */
	if (self->product_id != NULL) {
		gsize product_id_sz = strlen(self->product_id);
		if (!fu_memcpy_safe(buf->data,
				    buf->len,
				    FU_STRUCT_RMI_IMG_OFFSET_PRODUCT_ID, /* dst */
				    (const guint8 *)self->product_id,
				    product_id_sz,
				    0x0, /* src */
				    product_id_sz,
				    error))
			return NULL;
	}
	fu_memwrite_uint32(buf->data + FU_STRUCT_RMI_IMG_OFFSET_FW_BUILD_ID,
			   0x1234,
			   G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf->data + FU_STRUCT_RMI_IMG_OFFSET_PACKAGE_ID,
			   0x4321,
			   G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf->data + FU_STRUCT_RMI_IMG_OFFSET_PRODUCT_INFO,
			   0x3456,
			   G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf->data + FU_STRUCT_RMI_IMG_OFFSET_IMAGE_SIZE, bufsz, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf->data + FU_STRUCT_RMI_IMG_OFFSET_CONFIG_SIZE,
			   bufsz,
			   G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf->data + RMI_IMG_V10_CNTR_ADDR_OFFSET,
			   RMI_IMG_FW_OFFSET,
			   G_LITTLE_ENDIAN);

	/* hierarchical section */
	fu_struct_rmi_container_descriptor_set_container_id(desc_hdr,
							    RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL);
	fu_struct_rmi_container_descriptor_set_content_length(desc_hdr, 0x1 * 4); /* bytes */
	fu_struct_rmi_container_descriptor_set_content_address(desc_hdr,
							       RMI_IMG_FW_OFFSET +
								   0x20); /* offset to table */
	memcpy(buf->data + RMI_IMG_FW_OFFSET + 0x00, desc_hdr->data, desc_hdr->len);
	memcpy(buf->data + RMI_IMG_FW_OFFSET + 0x20, offset_table, sizeof(offset_table));
	memcpy(buf->data + RMI_IMG_FW_OFFSET + 0x24, desc->data, desc->len);
	fu_memwrite_uint32(buf->data + RMI_IMG_FW_OFFSET + 0x44,
			   0xfeed,
			   G_LITTLE_ENDIAN); /* flash_config */

	/* fixup checksum */
	csum = fu_synaptics_rmi_generate_checksum(buf->data + 4, buf->len - 4);
	fu_memwrite_uint32(buf->data + FU_STRUCT_RMI_IMG_OFFSET_CHECKSUM, csum, G_LITTLE_ENDIAN);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_synaptics_rmi_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	const gchar *product_id;
	guint64 tmp;

	/* either 0x or 10 */
	tmp = xb_node_query_text_as_uint(n, "kind", NULL);
	if (tmp != G_MAXUINT64)
		self->kind = tmp;

	/* any string */
	product_id = xb_node_query_text(n, "product_id", NULL);
	if (product_id != NULL) {
		gsize product_id_sz = strlen(product_id);
		if (product_id_sz > RMI_PRODUCT_ID_LENGTH) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "product_id not supported, %u of %u bytes",
				    (guint)product_id_sz,
				    (guint)RMI_PRODUCT_ID_LENGTH);
			return FALSE;
		}
		g_free(self->product_id);
		self->product_id = g_strdup(product_id);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_synaptics_rmi_firmware_write(FuFirmware *firmware, GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(firmware);

	/* two supported container formats */
	if (self->kind == RMI_FIRMWARE_KIND_0X)
		return fu_synaptics_rmi_firmware_write_v0x(firmware, error);
	if (self->kind == RMI_FIRMWARE_KIND_10)
		return fu_synaptics_rmi_firmware_write_v10(firmware, error);

	/* not supported */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "kind not supported");
	return NULL;
}

static void
fu_synaptics_rmi_firmware_init(FuSynapticsRmiFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_synaptics_rmi_firmware_finalize(GObject *obj)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE(obj);
	g_free(self->product_id);
	G_OBJECT_CLASS(fu_synaptics_rmi_firmware_parent_class)->finalize(obj);
}

static void
fu_synaptics_rmi_firmware_class_init(FuSynapticsRmiFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_synaptics_rmi_firmware_finalize;
	klass_firmware->parse = fu_synaptics_rmi_firmware_parse;
	klass_firmware->export = fu_synaptics_rmi_firmware_export;
	klass_firmware->build = fu_synaptics_rmi_firmware_build;
	klass_firmware->write = fu_synaptics_rmi_firmware_write;
}

FuFirmware *
fu_synaptics_rmi_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPTICS_RMI_FIRMWARE, NULL));
}
