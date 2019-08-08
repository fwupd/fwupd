/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2014 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-synaptics-rmi-common.h"
#include "fu-synaptics-rmi-firmware.h"

struct _FuSynapticsRmiFirmware {
	FuFirmware		 parent_instance;
	guint32			 checksum;
	guint8			 io;
	guint8			 bootloader_version;
	guint32			 build_id;
	guint16			 package_id;
	guint16			 product_info;
	gchar			*product_id;
};

G_DEFINE_TYPE (FuSynapticsRmiFirmware, fu_synaptics_rmi_firmware, FU_TYPE_FIRMWARE)

#define RMI_IMG_CHECKSUM_OFFSET			0
#define RMI_IMG_IO_OFFSET			0x06
#define RMI_IMG_BOOTLOADER_VERSION_OFFSET	0x07
#define RMI_IMG_IMAGE_SIZE_OFFSET		0x08
#define RMI_IMG_CONFIG_SIZE_OFFSET		0x0c
#define RMI_IMG_PACKAGE_ID_OFFSET		0x1a
#define RMI_IMG_FW_BUILD_ID_OFFSET		0x50

#define RMI_IMG_PRODUCT_ID_OFFSET		0x10
#define RMI_IMG_PRODUCT_INFO_OFFSET		0x1e

#define RMI_IMG_FW_OFFSET			0x100

#define RMI_IMG_LOCKDOWN_V2_OFFSET		0xd0
#define RMI_IMG_LOCKDOWN_V2_SIZE		0x30

#define RMI_IMG_LOCKDOWN_V3_OFFSET		0xc0
#define RMI_IMG_LOCKDOWN_V3_SIZE		0x40

#define RMI_IMG_LOCKDOWN_V5_OFFSET		0xb0
#define RMI_IMG_LOCKDOWN_V5_SIZE		0x50

#define RMI_IMG_V10_CNTR_ADDR_OFFSET		0x0c

typedef struct {
	guint8	 content_checksum[4];
	guint8	 container_id[2];
	guint8	 minor_version;
	guint8	 major_version;
	guint8	 reserved_08;
	guint8	 reserved_09;
	guint8	 reserved_0a;
	guint8	 reserved_0b;
	guint8	 container_option_flags[4];
	guint8	 content_options_length[4];
	guint8	 content_options_address[4];
	guint8	 content_length[4];
	guint8	 content_address[4];
} RmiFirmwareContainerDescriptor;

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
} RmiFirmwareContainerId;

static void
fu_synaptics_rmi_firmware_add_image (FuFirmware *firmware, const gchar *id,
				     const guint8 *data, gsize sz)
{
	g_autoptr(GBytes) bytes = g_bytes_new (data, sz);
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (bytes);
	if (id != NULL)
		fu_firmware_image_set_id (img, id);
	fu_firmware_add_image (firmware, img);
}

static void
fu_synaptics_rmi_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE (firmware);
	fu_common_string_append_kv (str, idt, "ProductId", self->product_id);
	fu_common_string_append_kx (str, idt, "BootloaderVersion", self->bootloader_version);
	fu_common_string_append_kx (str, idt, "IO", self->io);
	fu_common_string_append_kx (str, idt, "Checksum", self->checksum);
	fu_common_string_append_kx (str, idt, "BuildId", self->build_id);
	fu_common_string_append_kx (str, idt, "PackageId", self->package_id);
	fu_common_string_append_kx (str, idt, "ProductInfo", self->product_info);
}

static gboolean
fu_synaptics_rmi_firmware_hierarchical_parse (FuFirmware *firmware,
					      GBytes *fw,
					      GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE (firmware);
	RmiFirmwareContainerDescriptor desc;
	guint32 cntrs_len;
	guint32 offset;
	guint32 cntr_addr;
	gsize sz = 0;
	const guint8 *data = g_bytes_get_data (fw, &sz);

	memset (&desc, 0x0, sizeof(desc));
	cntr_addr = fu_common_read_uint32 (data + RMI_IMG_V10_CNTR_ADDR_OFFSET, G_LITTLE_ENDIAN);
	if (!fu_memcpy_safe ((guint8 *) &desc, sizeof(desc), 0x0,	/* dst */
			     data, sz, cntr_addr,			/* src */
			     sizeof(desc), error)) {
		g_prefix_error (error, "RmiFirmwareContainerDescriptor invalid: ");
		return FALSE;
	}
	offset = fu_common_read_uint32 (desc.content_address, G_LITTLE_ENDIAN);
	cntrs_len = fu_common_read_uint32 (desc.content_length, G_LITTLE_ENDIAN) / 4;

	for (guint32 i = 0; i < cntrs_len; i++) {
		const guint8 *content;
		guint16 container_id;
		guint32 addr = fu_common_read_uint32 (data + offset, G_LITTLE_ENDIAN);
		guint32 length;

		if (!fu_memcpy_safe ((guint8 *) &desc, sizeof(desc), 0x0,	/* dst */
				     data, sz, addr,				/* src */
				     sizeof(desc), error))
			return FALSE;
		container_id = fu_common_read_uint16 (desc.container_id, G_LITTLE_ENDIAN);
		content = data + fu_common_read_uint32 (desc.content_address, G_LITTLE_ENDIAN);
		length = fu_common_read_uint32 (desc.content_length, G_LITTLE_ENDIAN);
		switch (container_id) {
		case RMI_FIRMWARE_CONTAINER_ID_BL:
			self->bootloader_version = *content;
			break;
		case RMI_FIRMWARE_CONTAINER_ID_UI:
		case RMI_FIRMWARE_CONTAINER_ID_CORE_CODE:
			fu_synaptics_rmi_firmware_add_image (firmware, "ui", content, length);
			break;
		case RMI_FIRMWARE_CONTAINER_ID_FLASH_CONFIG:
			fu_synaptics_rmi_firmware_add_image (firmware, "flash-config", content, length);
			break;
		case RMI_FIRMWARE_CONTAINER_ID_UI_CONFIG:
		case RMI_FIRMWARE_CONTAINER_ID_CORE_CONFIG:
			fu_synaptics_rmi_firmware_add_image (firmware, "config", content, length);
			break;
		case RMI_FIRMWARE_CONTAINER_ID_PERMANENT_CONFIG:
		case RMI_FIRMWARE_CONTAINER_ID_GUEST_SERIALIZATION:
			fu_synaptics_rmi_firmware_add_image (firmware, "lockdown", content, length);
			break;
		case RMI_FIRMWARE_CONTAINER_ID_GENERAL_INFORMATION:
			self->io = 1;
			self->package_id = fu_common_read_uint32 (content, G_LITTLE_ENDIAN);
			self->build_id = fu_common_read_uint32 (content + 4, G_LITTLE_ENDIAN);
			self->product_id = g_strndup ((const gchar *) content + 0x18, RMI_PRODUCT_ID_LENGTH);
			break;
		default:
			break;
		}
		offset += 4;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_firmware_parse (FuFirmware *firmware,
				 GBytes *fw,
				 guint64 addr_start,
				 guint64 addr_end,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE (firmware);
	gsize sz = 0;
	guint32 cfg_sz;
	guint32 checksum_calculated;
	guint32 img_sz;
	const guint8 *data = g_bytes_get_data (fw, &sz);

	/* check minimum size */
	if (sz < 0x100) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "not enough data to parse header");
		return FALSE;
	}
	if (sz % 2 != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "data not aligned to 16 bits");
		return FALSE;
	}

	/* verify checksum */
	self->checksum = fu_common_read_uint32 (data + RMI_IMG_CHECKSUM_OFFSET, G_LITTLE_ENDIAN);
	checksum_calculated = fu_synaptics_rmi_generate_checksum (data + 4, sz - 4);
	if (self->checksum != checksum_calculated) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "checksum verification failed, got 0x%08x, actual 0x%08x\n",
			     (guint) self->checksum, (guint) checksum_calculated);
		return FALSE;
	}

	/* parse legacy image */
	self->io = data[RMI_IMG_IO_OFFSET];
	self->bootloader_version = data[RMI_IMG_BOOTLOADER_VERSION_OFFSET];
	if (self->io == 1) {
		self->build_id = fu_common_read_uint32 (data + RMI_IMG_FW_BUILD_ID_OFFSET, G_LITTLE_ENDIAN);
		self->package_id = fu_common_read_uint32 (data + RMI_IMG_PACKAGE_ID_OFFSET, G_LITTLE_ENDIAN);
	}
	self->product_id = g_strndup ((const gchar *) data + RMI_IMG_PRODUCT_ID_OFFSET, RMI_PRODUCT_ID_LENGTH);
	self->product_info = fu_common_read_uint16 (data + RMI_IMG_PRODUCT_INFO_OFFSET, G_LITTLE_ENDIAN);

	/* main firmware */
	img_sz = fu_common_read_uint32 (data + RMI_IMG_IMAGE_SIZE_OFFSET, G_LITTLE_ENDIAN);
	if (img_sz > 0) {
		fu_synaptics_rmi_firmware_add_image (firmware, "ui",
						     data + RMI_IMG_FW_OFFSET,
						     img_sz);
	}

	/* config */
	cfg_sz = fu_common_read_uint32 (data + RMI_IMG_CONFIG_SIZE_OFFSET, G_LITTLE_ENDIAN);
	if (cfg_sz > 0) {
		fu_synaptics_rmi_firmware_add_image (firmware, "config",
						     data + RMI_IMG_FW_OFFSET + img_sz,
						     cfg_sz);
	}

	switch (self->bootloader_version) {
	case 2:
		fu_synaptics_rmi_firmware_add_image (firmware, "lockdown",
						     data + RMI_IMG_LOCKDOWN_V2_OFFSET,
						     RMI_IMG_LOCKDOWN_V2_SIZE);
		break;
	case 3:
	case 4:
		fu_synaptics_rmi_firmware_add_image (firmware, "lockdown",
						     data + RMI_IMG_LOCKDOWN_V3_OFFSET,
						     RMI_IMG_LOCKDOWN_V3_SIZE);
		break;
	case 5:
	case 6:
		fu_synaptics_rmi_firmware_add_image (firmware, "lockdown",
						     data + RMI_IMG_LOCKDOWN_V5_OFFSET,
						     RMI_IMG_LOCKDOWN_V5_SIZE);
		break;
	case 16:
		if (!fu_synaptics_rmi_firmware_hierarchical_parse (firmware, fw, error))
			return FALSE;
		break;
	default:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "unsupported image version 0x%02x",
			     self->bootloader_version);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_synaptics_rmi_firmware_init (FuSynapticsRmiFirmware *self)
{
}

static void
fu_synaptics_rmi_firmware_class_init (FuSynapticsRmiFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_synaptics_rmi_firmware_parse;
	klass_firmware->to_string = fu_synaptics_rmi_firmware_to_string;
}

FuFirmware *
fu_synaptics_rmi_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_SYNAPTICS_RMI_FIRMWARE, NULL));
}
