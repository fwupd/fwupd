/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2019 Synaptics Inc.
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

#define RMI_IMG_CHECKSUM_OFFSET			0x00
#define RMI_IMG_IO_OFFSET			0x06
#define RMI_IMG_BOOTLOADER_VERSION_OFFSET	0x07
#define RMI_IMG_IMAGE_SIZE_OFFSET		0x08
#define RMI_IMG_CONFIG_SIZE_OFFSET		0x0c
#define RMI_IMG_PACKAGE_ID_OFFSET		0x1a
#define RMI_IMG_FW_BUILD_ID_OFFSET		0x50
#define RMI_IMG_PRODUCT_ID_OFFSET		0x10
#define RMI_IMG_PRODUCT_INFO_OFFSET		0x1e
#define RMI_IMG_FW_OFFSET			0x100

#define RMI_IMG_V10_CNTR_ADDR_OFFSET		0x0c

typedef struct __attribute__((packed)) {
	guint32	 content_checksum;
	guint16	 container_id;
	guint8	 minor_version;
	guint8	 major_version;
	guint8	 reserved_08;
	guint8	 reserved_09;
	guint8	 reserved_0a;
	guint8	 reserved_0b;
	guint32	 container_option_flags;
	guint32	 content_options_length;
	guint32	 content_options_address;
	guint32	 content_length;
	guint32	 content_address;
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

static const gchar *
rmi_firmware_container_id_to_string (RmiFirmwareContainerId container_id)
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
	return NULL;
}

static void
fu_synaptics_rmi_firmware_add_image (FuFirmware *firmware, const gchar *id,
				     const guint8 *data, gsize sz)
{
	g_autoptr(GBytes) bytes = g_bytes_new (data, sz);
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (bytes);
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
fu_synaptics_rmi_firmware_parse_v10 (FuFirmware *firmware, GBytes *fw, GError **error)
{
	FuSynapticsRmiFirmware *self = FU_SYNAPTICS_RMI_FIRMWARE (firmware);
	RmiFirmwareContainerDescriptor desc = { 0x0 };
	guint16 container_id;
	guint32 cntrs_len;
	guint32 offset;
	guint32 cntr_addr;
	gsize sz = 0;
	const guint8 *data = g_bytes_get_data (fw, &sz);

	cntr_addr = fu_common_read_uint32 (data + RMI_IMG_V10_CNTR_ADDR_OFFSET, G_LITTLE_ENDIAN);
	g_debug ("v10 RmiFirmwareContainerDescriptor at 0x%x", cntr_addr);
	if (!fu_memcpy_safe ((guint8 *) &desc, sizeof(desc), 0x0,	/* dst */
			     data, sz, cntr_addr,			/* src */
			     sizeof(desc), error)) {
		g_prefix_error (error, "RmiFirmwareContainerDescriptor invalid: ");
		return FALSE;
	}
	container_id = GUINT16_FROM_LE(desc.container_id);
	if (container_id != RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "toplevel container_id invalid, got 0x%x expected 0x%x",
			     (guint) container_id,
			     (guint) RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL);
		return FALSE;
	}
	offset = GUINT32_FROM_LE(desc.content_address);
	if (offset > sz - sizeof(guint32) - sizeof(desc)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "image offset invalid, got 0x%x, size 0x%x",
			     (guint) offset, (guint) sz);
		return FALSE;
	}
	cntrs_len = GUINT32_FROM_LE(desc.content_length) / 4;
	g_debug ("offset=0x%x (cntrs_len=%u)", offset, cntrs_len);

	for (guint32 i = 0; i < cntrs_len; i++) {
		guint32 content_addr;
		guint32 addr = fu_common_read_uint32 (data + offset, G_LITTLE_ENDIAN);
		guint32 length;
		g_debug ("parsing RmiFirmwareContainerDescriptor at 0x%x", addr);
		if (!fu_memcpy_safe ((guint8 *) &desc, sizeof(desc), 0x0,	/* dst */
				     data, sz, addr,				/* src */
				     sizeof(desc), error))
			return FALSE;
		container_id = GUINT16_FROM_LE(desc.container_id);
		content_addr = GUINT32_FROM_LE(desc.content_address);
		length = GUINT32_FROM_LE(desc.content_length);
		g_debug ("RmiFirmwareContainerDescriptor 0x%02x @ 0x%x (len 0x%x)",
			 container_id, content_addr, length);
		if (length > sz) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "length invalid, length 0x%x, size 0x%x",
				     (guint) length, (guint) sz);
			return FALSE;
		}
		if (content_addr > sz - length) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "address invalid, got 0x%x (length 0x%x), size 0x%x",
				     (guint) content_addr, (guint) length, (guint) sz);
			return FALSE;
		}
		switch (container_id) {
		case RMI_FIRMWARE_CONTAINER_ID_BL:
			self->bootloader_version = data[content_addr];
			break;
		case RMI_FIRMWARE_CONTAINER_ID_UI:
		case RMI_FIRMWARE_CONTAINER_ID_CORE_CODE:
			fu_synaptics_rmi_firmware_add_image (firmware, "ui",
							     data + content_addr, length);
			break;
		case RMI_FIRMWARE_CONTAINER_ID_FLASH_CONFIG:
			fu_synaptics_rmi_firmware_add_image (firmware, "flash-config",
							     data + content_addr, length);
			break;
		case RMI_FIRMWARE_CONTAINER_ID_UI_CONFIG:
		case RMI_FIRMWARE_CONTAINER_ID_CORE_CONFIG:
			fu_synaptics_rmi_firmware_add_image (firmware, "config",
							     data + content_addr, length);
			break;
		case RMI_FIRMWARE_CONTAINER_ID_GENERAL_INFORMATION:
			if (length < 0x18 + RMI_PRODUCT_ID_LENGTH) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "content_addr invalid, got 0x%x (length 0x%x)",
					     content_addr, (guint) length);
				return FALSE;
			}
			self->io = 1;
			self->package_id = fu_common_read_uint32 (data + content_addr, G_LITTLE_ENDIAN);
			self->build_id = fu_common_read_uint32 (data + content_addr + 4, G_LITTLE_ENDIAN);
			self->product_id = g_strndup ((const gchar *) data + content_addr + 0x18, RMI_PRODUCT_ID_LENGTH);
			break;
		default:
			g_debug ("unsupported container %s [0x%02x]",
				 rmi_firmware_container_id_to_string (container_id),
				 container_id);
			break;
		}
		offset += 4;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_firmware_parse_v0x (FuFirmware *firmware, GBytes *fw, GError **error)
{
	guint32 cfg_sz;
	guint32 img_sz;
	gsize sz = 0;
	const guint8 *data = g_bytes_get_data (fw, &sz);

	/* main firmware */
	img_sz = fu_common_read_uint32 (data + RMI_IMG_IMAGE_SIZE_OFFSET, G_LITTLE_ENDIAN);
	if (img_sz > 0) {
		if (img_sz > sz - RMI_IMG_FW_OFFSET) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "img_sz offset invalid, got 0x%x, size 0x%x",
				     (guint) img_sz, (guint) sz - RMI_IMG_FW_OFFSET);
			return FALSE;
		}
		fu_synaptics_rmi_firmware_add_image (firmware, "ui",
						     data + RMI_IMG_FW_OFFSET,
						     img_sz);
	}

	/* config */
	cfg_sz = fu_common_read_uint32 (data + RMI_IMG_CONFIG_SIZE_OFFSET, G_LITTLE_ENDIAN);
	if (cfg_sz > 0) {
		if (cfg_sz > sz - RMI_IMG_FW_OFFSET) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "cfg_sz offset invalid, got 0x%x, size 0x%x",
				     (guint) cfg_sz, (guint) sz - RMI_IMG_FW_OFFSET);
			return FALSE;
		}
		fu_synaptics_rmi_firmware_add_image (firmware, "config",
						     data + RMI_IMG_FW_OFFSET + img_sz,
						     cfg_sz);
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
	guint32 checksum_calculated;
	const guint8 *data = g_bytes_get_data (fw, &sz);

	/* check minimum size */
	if (sz < RMI_IMG_FW_OFFSET) {
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
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		if (self->checksum != checksum_calculated) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "checksum verification failed, got 0x%08x, actual 0x%08x",
				     (guint) self->checksum, (guint) checksum_calculated);
			return FALSE;
		}
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

	/* parse partitions, but ignore lockdown */
	switch (self->bootloader_version) {
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
		if (!fu_synaptics_rmi_firmware_parse_v0x (firmware, fw, error))
			return FALSE;
		break;
	case 16:
		if (!fu_synaptics_rmi_firmware_parse_v10 (firmware, fw, error))
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

GBytes *
fu_synaptics_rmi_firmware_generate_v0x (void)
{
	GByteArray *buf = g_byte_array_new ();

	/* create empty block */
	g_byte_array_set_size (buf, RMI_IMG_FW_OFFSET + 0x4 + 0x4);
	buf->data[RMI_IMG_IO_OFFSET] = 0x0;			/* no build_id or package_id */
	buf->data[RMI_IMG_BOOTLOADER_VERSION_OFFSET] = 0x2;	/* not hierarchical */
	memcpy (buf->data + RMI_IMG_PRODUCT_ID_OFFSET, "Example", 7);
	fu_common_write_uint16 (buf->data + RMI_IMG_PRODUCT_INFO_OFFSET, 0x1234, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + RMI_IMG_IMAGE_SIZE_OFFSET, 0x4, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + RMI_IMG_CONFIG_SIZE_OFFSET, 0x4, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + RMI_IMG_FW_OFFSET + 0x0, 0xdead, G_LITTLE_ENDIAN);	/* img */
	fu_common_write_uint32 (buf->data + RMI_IMG_FW_OFFSET + 0x4, 0xbeef, G_LITTLE_ENDIAN);	/* config */
	fu_common_dump_full (G_LOG_DOMAIN, "v0x", buf->data, buf->len,
			     0x20, FU_DUMP_FLAGS_SHOW_ADDRESSES);
	return g_byte_array_free_to_bytes (buf);
}

GBytes *
fu_synaptics_rmi_firmware_generate_v10 (void)
{
	GByteArray *buf = g_byte_array_new ();
	/* header | desc_hdr | offset_table | desc | flash_config |
	 *        \0x0       \0x20          \0x24  \0x44          |0x48 */
	RmiFirmwareContainerDescriptor desc_hdr = {
		.container_id =		GUINT16_TO_LE(RMI_FIRMWARE_CONTAINER_ID_TOP_LEVEL),
		.content_length =	GUINT32_TO_LE(0x1 * 4),				/* size of offset table in bytes */
		.content_address =	GUINT32_TO_LE(RMI_IMG_FW_OFFSET + 0x20),	/* offset to table */
	};
	guint32 offset_table[] = { RMI_IMG_FW_OFFSET + 0x24 };				/* offset to first RmiFirmwareContainerDescriptor */
	RmiFirmwareContainerDescriptor desc = {
		.container_id =		GUINT16_TO_LE(RMI_FIRMWARE_CONTAINER_ID_FLASH_CONFIG),
		.content_length =	GUINT32_TO_LE(0x4),
		.content_address =	GUINT32_TO_LE(RMI_IMG_FW_OFFSET + 0x44),
	};

	/* create empty block */
	g_byte_array_set_size (buf, RMI_IMG_FW_OFFSET + 0x48);
	buf->data[RMI_IMG_IO_OFFSET] = 0x1;
	buf->data[RMI_IMG_BOOTLOADER_VERSION_OFFSET] = 16;	/* hierarchical */
	memcpy (buf->data + RMI_IMG_PRODUCT_ID_OFFSET, "Example", 7);
	fu_common_write_uint32 (buf->data + RMI_IMG_FW_BUILD_ID_OFFSET, 0x1234, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + RMI_IMG_PACKAGE_ID_OFFSET, 0x4321, G_LITTLE_ENDIAN);
	fu_common_write_uint16 (buf->data + RMI_IMG_PRODUCT_INFO_OFFSET, 0x3456, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + RMI_IMG_IMAGE_SIZE_OFFSET, 0x4, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + RMI_IMG_CONFIG_SIZE_OFFSET, 0x4, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + RMI_IMG_V10_CNTR_ADDR_OFFSET, RMI_IMG_FW_OFFSET, G_LITTLE_ENDIAN);

	/* hierarchical section */
	memcpy (buf->data + RMI_IMG_FW_OFFSET + 0x00, &desc_hdr, sizeof(desc_hdr));
	memcpy (buf->data + RMI_IMG_FW_OFFSET + 0x20, offset_table, sizeof(offset_table));
	memcpy (buf->data + RMI_IMG_FW_OFFSET + 0x24, &desc, sizeof(desc));
	fu_common_write_uint32 (buf->data + RMI_IMG_FW_OFFSET + 0x44, 0xfeed, G_LITTLE_ENDIAN);	/* flash_config */
	fu_common_dump_full (G_LOG_DOMAIN, "v10", buf->data, buf->len,
			     0x20, FU_DUMP_FLAGS_SHOW_ADDRESSES);
	return g_byte_array_free_to_bytes (buf);
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
