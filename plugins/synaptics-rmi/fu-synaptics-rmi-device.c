/*
 * Copyright (C) 2012 Andrew Duggan
 * Copyright (C) 2012 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-rmi-common.h"
#include "fu-synaptics-rmi-firmware.h"
#include "fu-synaptics-rmi-ps2-device.h"
#include "fu-synaptics-rmi-v5-device.h"
#include "fu-synaptics-rmi-v6-device.h"
#include "fu-synaptics-rmi-v7-device.h"

#define RMI_DEVICE_PAGE_SIZE				0x100
#define RMI_DEVICE_PAGE_SCAN_START			0x00e9
#define RMI_DEVICE_PAGE_SCAN_END			0x0005
#define RMI_DEVICE_F01_BASIC_QUERY_LEN			11

#define RMI_DEVICE_F01_LTS_RESERVED_SIZE		19

#define RMI_DEVICE_F01_QRY1_HAS_LTS			(1 << 2)
#define RMI_DEVICE_F01_QRY1_HAS_SENSOR_ID		(1 << 3)
#define RMI_DEVICE_F01_QRY1_HAS_PROPS_2			(1 << 7)

#define RMI_DEVICE_F01_QRY42_DS4_QUERIES		(1 << 0)
#define RMI_DEVICE_F01_QRY43_01_PACKAGE_ID		(1 << 0)
#define RMI_DEVICE_F01_QRY43_01_BUILD_ID		(1 << 1)

#define RMI_F34_COMMAND_MASK				0x0f
#define RMI_F34_STATUS_MASK				0x07
#define RMI_F34_STATUS_SHIFT				4
#define RMI_F34_ENABLED_MASK				0x80

#define RMI_F34_COMMAND_V1_MASK				0x3f
#define RMI_F34_STATUS_V1_MASK				0x3f
#define RMI_F34_ENABLED_V1_MASK				0x80

#define RMI_F01_CMD_DEVICE_RESET			1
#define RMI_F01_DEFAULT_RESET_DELAY_MS			100

typedef struct
{
	FuSynapticsRmiFlash	 flash;
	GPtrArray		*functions;
	FuSynapticsRmiFunction	*f01;
	FuSynapticsRmiFunction	*f34;
	guint8			 current_page;
	guint16			 sig_size;	/* 0x0 for non-secure update */
	guint8			 max_page;
	gboolean		 in_iep_mode;
} FuSynapticsRmiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuSynapticsRmiDevice, fu_synaptics_rmi_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_synaptics_rmi_device_get_instance_private (o))

FuSynapticsRmiFlash *
fu_synaptics_rmi_device_get_flash (FuSynapticsRmiDevice *self)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	return &priv->flash;
}

static void
fu_synaptics_rmi_flash_to_string (FuSynapticsRmiFlash *flash, guint idt, GString *str)
{
	if (flash->bootloader_id[0] != 0x0) {
		g_autofree gchar *tmp = g_strdup_printf ("%02x.%02x",
							 flash->bootloader_id[0],
							 flash->bootloader_id[1]);
		fu_common_string_append_kv (str, idt, "BootloaderId", tmp);
	}
	fu_common_string_append_kx (str, idt, "BlockSize", flash->block_size);
	fu_common_string_append_kx (str, idt, "BlockCountFw", flash->block_count_fw);
	fu_common_string_append_kx (str, idt, "BlockCountCfg", flash->block_count_cfg);
	fu_common_string_append_kx (str, idt, "FlashConfigLength", flash->config_length);
	fu_common_string_append_kx (str, idt, "PayloadLength", flash->payload_length);
	fu_common_string_append_kx (str, idt, "BuildID", flash->build_id);
}

static void
fu_synaptics_rmi_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS (fu_synaptics_rmi_device_parent_class)->to_string (device, idt, str);

	fu_common_string_append_kx (str, idt, "CurrentPage", priv->current_page);
	fu_common_string_append_kx (str, idt, "InIepMode", priv->in_iep_mode);
	fu_common_string_append_kx (str, idt, "MaxPage", priv->max_page);
	fu_common_string_append_kx (str, idt, "SigSize", priv->sig_size);
	if (priv->f34 != NULL) {
		fu_common_string_append_kx (str, idt, "BlVer",
					    priv->f34->function_version + 0x5);
	}
	fu_synaptics_rmi_flash_to_string (&priv->flash, idt, str);
}

FuSynapticsRmiFunction *
fu_synaptics_rmi_device_get_function (FuSynapticsRmiDevice *self,
				      guint8 function_number,
				      GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	if (priv->functions->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no RMI functions, perhaps read the PDT?");
		return NULL;
	}
	for (guint i = 0; i < priv->functions->len; i++) {
		FuSynapticsRmiFunction *func = g_ptr_array_index (priv->functions, i);
		if (func->function_number == function_number)
			return func;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INTERNAL,
		     "failed to get RMI function 0x%02x",
		     function_number);
	return NULL;
}

GByteArray *
fu_synaptics_rmi_device_read (FuSynapticsRmiDevice *self,
			      guint16 addr,
			      gsize req_sz,
			      GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	return klass_rmi->read (self, addr, req_sz, error);
}

GByteArray *
fu_synaptics_rmi_device_read_packet_register (FuSynapticsRmiDevice *self,
					      guint16 addr,
					      gsize req_sz,
					      GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	if (klass_rmi->read_packet_register == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "packet register reads not supported");
		return NULL;
	}
	return klass_rmi->read_packet_register (self, addr, req_sz, error);
}

gboolean
fu_synaptics_rmi_device_write (FuSynapticsRmiDevice *self,
			       guint16 addr,
			       GByteArray *req,
			       FuSynapticsRmiDeviceFlags flags,
			       GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	return klass_rmi->write (self, addr, req, flags, error);
}

gboolean
fu_synaptics_rmi_device_set_page (FuSynapticsRmiDevice *self, guint8 page, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	if (priv->current_page == page)
		return TRUE;
	if (!klass_rmi->set_page (self, page, error))
		return FALSE;
	priv->current_page = page;
	return TRUE;
}

void
fu_synaptics_rmi_device_set_iepmode (FuSynapticsRmiDevice *self, gboolean iepmode)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	priv->in_iep_mode = iepmode;
}

gboolean
fu_synaptics_rmi_device_get_iepmode (FuSynapticsRmiDevice *self)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	return priv->in_iep_mode;
}

gboolean
fu_synaptics_rmi_device_write_bus_select (FuSynapticsRmiDevice *self, guint8 bus, GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	if (klass_rmi->write_bus_select == NULL)
		return TRUE;
	return klass_rmi->write_bus_select (self, bus, error);
}

gboolean
fu_synaptics_rmi_device_reset (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fu_byte_array_append_uint8 (req, RMI_F01_CMD_DEVICE_RESET);
	if (!fu_synaptics_rmi_device_write (self, priv->f01->command_base, req,
					    FU_SYNAPTICS_RMI_DEVICE_FLAG_ALLOW_FAILURE,
					    error))
		return FALSE;
	g_usleep (1000 * RMI_F01_DEFAULT_RESET_DELAY_MS);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_scan_pdt (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint interrupt_count = 0;

	/* clear old list */
	g_ptr_array_set_size (priv->functions, 0);

	/* scan pages */
	for (guint page = 0; page < priv->max_page; page++) {
		gboolean found = FALSE;
		guint32 page_start = RMI_DEVICE_PAGE_SIZE * page;
		guint32 pdt_start = page_start + RMI_DEVICE_PAGE_SCAN_START;
		guint32 pdt_end = page_start + RMI_DEVICE_PAGE_SCAN_END;

		/* set page */
		if (!fu_synaptics_rmi_device_set_page (self, page, error))
			return FALSE;

		/* read out functions */
		for (guint addr = pdt_start; addr >= pdt_end; addr -= RMI_DEVICE_PDT_ENTRY_SIZE) {
			g_autofree FuSynapticsRmiFunction *func = NULL;
			g_autoptr(GByteArray) res = NULL;
			res = fu_synaptics_rmi_device_read (self, addr, RMI_DEVICE_PDT_ENTRY_SIZE, error);
			if (res == NULL) {
				g_prefix_error (error,
						"failed to read page %u PDT entry @ 0x%04x: ",
						page, addr);
				return FALSE;
			}
			func = fu_synaptics_rmi_function_parse (res, page_start, interrupt_count, error);
			if (func == NULL)
				return FALSE;
			if (func->function_number == 0)
				break;
			interrupt_count += func->interrupt_source_count;
			g_ptr_array_add (priv->functions, g_steal_pointer (&func));
			found = TRUE;
		}
		if (!found)
			break;
	}

	/* for debug */
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		for (guint i = 0; i < priv->functions->len; i++) {
			FuSynapticsRmiFunction *func = g_ptr_array_index (priv->functions, i);
			g_debug ("PDT-%02u fn:0x%02x vr:%d sc:%d ms:0x%x "
				 "db:0x%02x cb:0x%02x cm:0x%02x qb:0x%02x",
				 i, func->function_number, func->function_version,
				 func->interrupt_source_count,
				 func->interrupt_mask,
				 func->data_base,
				 func->control_base, func->command_base,
				 func->query_base);
		}
	}

	/* success */
	return TRUE;
}

void
fu_synaptics_rmi_device_set_sig_size (FuSynapticsRmiDevice *self,
					guint16 sig_size)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	priv->sig_size = sig_size;
}

guint16
fu_synaptics_rmi_device_get_sig_size (FuSynapticsRmiDevice *self)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	return priv->sig_size;
}

void
fu_synaptics_rmi_device_set_max_page (FuSynapticsRmiDevice *self,
				      guint8 max_page)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	priv->max_page = max_page;
}

guint8
fu_synaptics_rmi_device_get_max_page (FuSynapticsRmiDevice *self)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	return priv->max_page;
}

static void
fu_synaptics_rmi_device_set_product_id (FuSynapticsRmiDevice *self, const gchar *product_id)
{
	g_autofree gchar *instance_id = NULL;
	g_auto(GStrv) product_id_split = g_strsplit (product_id, "-", 2);

	/* use the product ID as an instance ID */
	instance_id = g_strdup_printf ("SYNAPTICS_RMI\\%s", product_id);
	fu_device_add_instance_id (FU_DEVICE (self), instance_id);

	/* also add the product ID without the sub-number */
	if (g_strv_length (product_id_split) == 2) {
		g_autofree gchar *instance_id_major = NULL;
		instance_id_major = g_strdup_printf ("SYNAPTICS_RMI\\%s", product_id_split[0]);
		fu_device_add_instance_id (FU_DEVICE (self), instance_id_major);
	}
}

static gboolean
fu_synaptics_rmi_device_query_status (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	return klass_rmi->query_status (self, error);
}

static gboolean
fu_synaptics_rmi_device_query_build_id (FuSynapticsRmiDevice *self,
					guint32 *build_id,
					GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	if (klass_rmi->query_build_id == NULL)
		return TRUE;
	return klass_rmi->query_build_id (self, build_id, error);
}

static gboolean
fu_synaptics_rmi_device_query_product_sub_id (FuSynapticsRmiDevice *self,
					      guint8 *product_sub_id,
					      GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	if (klass_rmi->query_product_sub_id == NULL)
		return TRUE;
	return klass_rmi->query_product_sub_id (self, product_sub_id, error);
}

static gboolean
fu_synaptics_rmi_device_setup (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint16 addr;
	guint16 prod_info_addr;
	guint8 ds4_query_length = 0;
	guint8 product_sub_id = 0;
	gboolean has_build_id_query = FALSE;
	gboolean has_dds4_queries = FALSE;
	gboolean has_lts;
	gboolean has_package_id_query = FALSE;
	gboolean has_query42;
	gboolean has_sensor_id;
	g_autofree gchar *bl_ver = NULL;
	g_autofree gchar *fw_ver = NULL;
	g_autofree gchar *product_id = NULL;
	g_autoptr(GByteArray) f01_basic = NULL;
	g_autoptr(GByteArray) f01_product_id = NULL;
	g_autoptr(GByteArray) f01_ds4 = NULL;

	/* assume reset */
	priv->in_iep_mode = FALSE;

	/* read PDT */
	if (!fu_synaptics_rmi_device_scan_pdt (self, error))
		return FALSE;
	priv->f01 = fu_synaptics_rmi_device_get_function (self, 0x01, error);
	if (priv->f01 == NULL)
		return FALSE;
	addr = priv->f01->query_base;

	/* set page */
	if (!fu_synaptics_rmi_device_set_page (self, 0, error))
		return FALSE;

	/* force entering iep mode again */
	if (!fu_synaptics_rmi_device_enter_iep_mode (self, FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE, error))
		return FALSE;

	f01_basic = fu_synaptics_rmi_device_read (self, addr, RMI_DEVICE_F01_BASIC_QUERY_LEN, error);
	if (f01_basic == NULL) {
		g_prefix_error (error, "failed to read the basic query: ");
		return FALSE;
	}
	has_lts = (f01_basic->data[1] & RMI_DEVICE_F01_QRY1_HAS_LTS) > 0;
	has_sensor_id = (f01_basic->data[1] & RMI_DEVICE_F01_QRY1_HAS_SENSOR_ID) > 0;
	has_query42 = (f01_basic->data[1] & RMI_DEVICE_F01_QRY1_HAS_PROPS_2) > 0;

	/* get the product ID */
	addr += 11;
	f01_product_id = fu_synaptics_rmi_device_read (self, addr, RMI_PRODUCT_ID_LENGTH, error);
	if (f01_product_id == NULL) {
		g_prefix_error (error, "failed to read the product id: ");
		return FALSE;
	}
	if (!fu_synaptics_rmi_device_query_product_sub_id (self, &product_sub_id, error)) {
		g_prefix_error (error, "failed to query product sub id: ");
		return FALSE;
	}
	if (product_sub_id == 0) {
		/* HID */
		product_id = g_strndup ((const gchar *) f01_product_id->data,
					f01_product_id->len);
	} else {
		/* PS/2 */
		g_autofree gchar *tmp = g_strndup ((const gchar *) f01_product_id->data, 6);
		product_id = g_strdup_printf ("%s-%03d", tmp, product_sub_id);
	}
	if (product_id != NULL)
		fu_synaptics_rmi_device_set_product_id (self, product_id);

	/* force entering iep mode again */
	if (!fu_synaptics_rmi_device_enter_iep_mode (self, FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE, error))
		return FALSE;

	/* skip */
	prod_info_addr = addr + 6;
	addr += 10;
	if (has_lts)
		addr++;
	if (has_sensor_id)
		addr++;
	if (has_lts)
		addr += RMI_DEVICE_F01_LTS_RESERVED_SIZE;

	/* read package ids */
	if (has_query42) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, addr++, 1, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read query 42: ");
			return FALSE;
		}
		has_dds4_queries = (f01_tmp->data[0] & RMI_DEVICE_F01_QRY42_DS4_QUERIES) > 0;
	}
	if (has_dds4_queries) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, addr++, 1, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read DS4 query length: ");
			return FALSE;
		}
		ds4_query_length = f01_tmp->data[0];
	}
	f01_ds4 = fu_synaptics_rmi_device_read (self, addr, 0x1, error);
	if (f01_ds4 == NULL) {
		g_prefix_error (error, "failed to read F01 Query43: ");
		return FALSE;
	}
	has_package_id_query = (f01_ds4->data[0] & RMI_DEVICE_F01_QRY43_01_PACKAGE_ID) > 0;
	has_build_id_query = (f01_ds4->data[0] & RMI_DEVICE_F01_QRY43_01_BUILD_ID) > 0;
	addr += ds4_query_length;
	if (has_package_id_query)
		prod_info_addr++;
	if (has_build_id_query) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		guint8 buf32[4] = { 0x0 };
		f01_tmp = fu_synaptics_rmi_device_read (self, prod_info_addr, 0x3, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read build ID bytes: ");
			return FALSE;
		}
		if (!fu_memcpy_safe (buf32, sizeof(buf32), 0x0,		/* dst */
				     f01_tmp->data, f01_tmp->len, 0x0,	/* src */
				     f01_tmp->len, error))
			return FALSE;
		if (!fu_common_read_uint32_safe (buf32, sizeof(buf32), 0x0,
						 &priv->flash.build_id,
						 G_LITTLE_ENDIAN, error))
			return FALSE;
	}

	/* read build ID, typically only for PS/2 */
	if (!fu_synaptics_rmi_device_query_build_id (self,
						     &priv->flash.build_id,
						     error)) {
		g_prefix_error (error, "failed to query build id: ");
		return FALSE;
	}

	/* get Function34_Query0,1 */
	priv->f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (priv->f34 == NULL)
		return FALSE;
	if (priv->f34->function_version == 0x0) {
		if (!fu_synaptics_rmi_v5_device_setup (self, error)) {
			g_prefix_error (error, "failed to do v5 setup: ");
			return FALSE;
		}
	} else if (priv->f34->function_version == 0x1) {
		if (!fu_synaptics_rmi_v6_device_setup (self, error)) {
			g_prefix_error (error, "failed to do v6 setup: ");
			return FALSE;
		}
	} else if (priv->f34->function_version == 0x2) {
		if (!fu_synaptics_rmi_v7_device_setup (self, error)) {
			g_prefix_error (error, "failed to do v7 setup: ");
			return FALSE;
		}
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "f34 function version 0x%02x unsupported",
			     priv->f34->function_version);
		return FALSE;
	}
	if (!fu_synaptics_rmi_device_query_status (self, error)) {
		g_prefix_error (error, "failed to read bootloader status: ");
		return FALSE;
	}

	/* set versions */
	fw_ver = g_strdup_printf ("%u.%u.%u",
				  f01_basic->data[2],
				  f01_basic->data[3],
				  priv->flash.build_id);
	fu_device_set_version (device, fw_ver);
	bl_ver = g_strdup_printf ("%u.0.0", priv->flash.bootloader_id[1]);
	fu_device_set_version_bootloader (device, bl_ver);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_synaptics_rmi_device_prepare_firmware (FuDevice *device,
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(FuFirmware) firmware = fu_synaptics_rmi_firmware_new ();
	g_autoptr(GBytes) bytes_cfg = NULL;
	g_autoptr(GBytes) bytes_bin = NULL;
	gsize size_expected;

	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check sizes */
	bytes_bin = fu_firmware_get_image_by_id_bytes (firmware, "ui", error);
	if (bytes_bin == NULL)
		return NULL;
	size_expected = ((gsize) priv->flash.block_count_fw * (gsize) priv->flash.block_size) +
			fu_synaptics_rmi_firmware_get_sig_size (FU_SYNAPTICS_RMI_FIRMWARE (firmware));
	if (g_bytes_get_size (bytes_bin) != size_expected) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "file firmware invalid size 0x%04x, expected 0x%04x",
			     (guint) g_bytes_get_size (bytes_bin),
			     (guint) size_expected);
		return NULL;
	}
	bytes_cfg = fu_firmware_get_image_by_id_bytes (firmware, "config", error);
	if (bytes_cfg == NULL)
		return NULL;
	size_expected = (gsize) priv->flash.block_count_cfg * (gsize) priv->flash.block_size;
	if (g_bytes_get_size (bytes_cfg) != size_expected) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "file config invalid size 0x%04x, expected 0x%04x",
			     (guint) g_bytes_get_size (bytes_cfg),
			     (guint) size_expected);
		return NULL;
	}

	return g_steal_pointer (&firmware);
}

static gboolean
fu_synaptics_rmi_device_poll (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) f34_db = NULL;

	/* get if the last flash read completed successfully */
	f34_db = fu_synaptics_rmi_device_read (self, priv->f34->data_base, 0x1, error);
	if (f34_db == NULL) {
		g_prefix_error (error, "failed to read f34_db: ");
		return FALSE;
	}
	if ((f34_db->data[0] & 0x1f) != 0x0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "flash status invalid: 0x%x",
			     (guint) (f34_db->data[0] & 0x1f));
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_rmi_device_poll_wait (FuSynapticsRmiDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* try to poll every 20ms for up to 400ms */
	for (guint i = 0; i < 20; i++) {
		g_usleep (1000 * 20);
		g_clear_error (&error_local);
		if (fu_synaptics_rmi_device_poll (self, &error_local))
			return TRUE;
		g_debug ("failed: %s", error_local->message);
	}

	/* proxy the last error */
	g_propagate_error (error, g_steal_pointer (&error_local));
	return FALSE;
}

static gboolean
fu_synaptics_rmi_device_wait_for_attr (FuSynapticsRmiDevice *self,
				       guint8 source_mask,
				       guint timeout_ms,
				       GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	return klass_rmi->wait_for_attr (self, source_mask, timeout_ms, error);
}

gboolean
fu_synaptics_rmi_device_enter_iep_mode (FuSynapticsRmiDevice *self,
					FuSynapticsRmiDeviceFlags flags,
					GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);

	/* already set */
	if ((flags & FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE) == 0 && priv->in_iep_mode)
		return TRUE;
	if (klass_rmi->enter_iep_mode != NULL) {
		g_debug ("enabling RMI iep_mode");
		if (!klass_rmi->enter_iep_mode (self, error)) {
			g_prefix_error (error, "failed to enable RMI iep_mode: ");
			return FALSE;
		}
	}
	priv->in_iep_mode = TRUE;
	return TRUE;
}

gboolean
fu_synaptics_rmi_device_wait_for_idle (FuSynapticsRmiDevice *self,
				       guint timeout_ms,
				       RmiDeviceWaitForIdleFlags flags,
				       GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint8 f34_command;
	guint8 f34_enabled;
	guint8 f34_status;
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(GError) error_local = NULL;

	/* try to get report without requesting */
	if (timeout_ms > 0 &&
	    !fu_synaptics_rmi_device_wait_for_attr (self,
						    priv->f34->interrupt_mask,
						    timeout_ms,
						    &error_local)) {
		if (!g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_prefixed_error (error,
						    g_steal_pointer (&error_local),
						    "failed to wait for attr: ");
			return FALSE;
		}
	} else if ((flags & RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34) == 0) {
		/* device reported idle via an event */
		return TRUE;
	}

	/* if for some reason we are not getting attention reports for HID devices
	 * then we can still continue after the timeout and read F34 status
	 * but if we have to wait for the timeout to ellapse every time then this
	 * will be slow */
	if (priv->f34->function_version == 0x1) {
		res = fu_synaptics_rmi_device_read (self, priv->flash.status_addr, 0x2, error);
		if (res == NULL)
			return FALSE;
		f34_command = res->data[0] & RMI_F34_COMMAND_V1_MASK;
		f34_status = res->data[1] & RMI_F34_STATUS_V1_MASK;
		f34_enabled = !!(res->data[1] & RMI_F34_ENABLED_MASK);
	} else {
		res = fu_synaptics_rmi_device_read (self, priv->flash.status_addr, 0x1, error);
		if (res == NULL)
			return FALSE;
		f34_command = res->data[0] & RMI_F34_COMMAND_MASK;
		f34_status = (res->data[0] >> RMI_F34_STATUS_SHIFT) & RMI_F34_STATUS_MASK;
		f34_enabled = !!(res->data[0] & RMI_F34_ENABLED_MASK);
	}

	/* PS/2 */
	if (FU_IS_SYNAPTICS_RMI_PS2_DEVICE (self)) {
		if (f34_command == 0) {
			g_debug ("F34 zero as PS/2");
			return TRUE;
		}
	}

	/* is idle */
	if (f34_status == 0x0 && f34_command == 0x0) {
		if (f34_enabled == 0x0) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "idle but enabled unset");
			return FALSE;
		}
		return TRUE;
	}

	/* failed */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "timed out waiting for idle [cmd:0x%x, sta:0x%x, ena:0x%x]",
		     f34_command, f34_status, f34_enabled);
	return FALSE;
}

gboolean
fu_synaptics_rmi_device_disable_sleep (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (self);
	if (klass_rmi->disable_sleep == NULL)
		return TRUE;
	return klass_rmi->disable_sleep (self, error);
}

gboolean
fu_synaptics_rmi_device_write_bootloader_id (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	gint block_data_offset = RMI_F34_BLOCK_DATA_OFFSET;
	g_autoptr(GByteArray) bootloader_id_req = g_byte_array_new ();

	if (priv->f34->function_version == 0x1)
		block_data_offset = RMI_F34_BLOCK_DATA_V1_OFFSET;

	/* write bootloader_id into F34_Flash_Data0,1 */
	g_byte_array_append (bootloader_id_req, priv->flash.bootloader_id, sizeof(priv->flash.bootloader_id));
	if (!fu_synaptics_rmi_device_write (self,
					    priv->f34->data_base + block_data_offset,
					    bootloader_id_req,
					    FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					    error)) {
		g_prefix_error (error, "failed to write bootloader_id: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_rmi_device_disable_irqs (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) interrupt_disable_req = g_byte_array_new ();

	fu_byte_array_append_uint8 (interrupt_disable_req,
				    priv->f34->interrupt_mask | priv->f01->interrupt_mask);
	if (!fu_synaptics_rmi_device_write (self,
					    priv->f01->control_base + 1,
					    interrupt_disable_req,
					    FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					    error)) {
		g_prefix_error (error, "failed to disable interrupts: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	if (priv->f34->function_version == 0x0 ||
	    priv->f34->function_version == 0x1) {
		return fu_synaptics_rmi_v5_device_write_firmware(device,
								 firmware,
								 progress,
								 flags,
								 error);
	}
	if (priv->f34->function_version == 0x2) {
		return fu_synaptics_rmi_v7_device_write_firmware(device,
								 firmware,
								 progress,
								 flags,
								 error);
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "f34 function version 0x%02x unsupported",
		     priv->f34->function_version);
	return FALSE;
}

static void
fu_synaptics_rmi_device_init (FuSynapticsRmiDevice *self)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	fu_device_add_protocol (FU_DEVICE (self), "com.synaptics.rmi");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	priv->current_page = 0xfe;
	priv->functions = g_ptr_array_new_with_free_func (g_free);
}

static void
fu_synaptics_rmi_device_finalize (GObject *object)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (object);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_ptr_array_unref (priv->functions);
	G_OBJECT_CLASS (fu_synaptics_rmi_device_parent_class)->finalize (object);
}

static void
fu_synaptics_rmi_device_class_init (FuSynapticsRmiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_synaptics_rmi_device_finalize;
	klass_device->to_string = fu_synaptics_rmi_device_to_string;
	klass_device->prepare_firmware = fu_synaptics_rmi_device_prepare_firmware;
	klass_device->setup = fu_synaptics_rmi_device_setup;
	klass_device->write_firmware = fu_synaptics_rmi_device_write_firmware;
}
