/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2019 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/ioctl.h>
#include <linux/hidraw.h>

#include "fu-io-channel.h"

#include "fu-synaptics-rmi-common.h"
#include "fu-synaptics-rmi-firmware.h"
#include "fu-synaptics-rmi-v5-device.h"
#include "fu-synaptics-rmi-v6-device.h"
#include "fu-synaptics-rmi-v7-device.h"

#define RMI_WRITE_REPORT_ID				0x9	/* output report */
#define RMI_READ_ADDR_REPORT_ID				0xa	/* output report */
#define RMI_READ_DATA_REPORT_ID				0xb	/* input report */
#define RMI_ATTN_REPORT_ID				0xc	/* input report */
#define RMI_SET_RMI_MODE_REPORT_ID			0xf	/* feature report */

#define RMI_DEVICE_DEFAULT_TIMEOUT			2000

#define HID_RMI4_REPORT_ID				0
#define HID_RMI4_READ_INPUT_COUNT			1
#define HID_RMI4_READ_INPUT_DATA			2
#define HID_RMI4_READ_OUTPUT_ADDR			2
#define HID_RMI4_READ_OUTPUT_COUNT			4
#define HID_RMI4_WRITE_OUTPUT_COUNT			1
#define HID_RMI4_WRITE_OUTPUT_ADDR			2
#define HID_RMI4_WRITE_OUTPUT_DATA			4
#define HID_RMI4_FEATURE_MODE				1
#define HID_RMI4_ATTN_INTERUPT_SOURCES			1
#define HID_RMI4_ATTN_DATA				2

#define RMI_DEVICE_PAGE_SELECT_REGISTER			0xff
#define RMI_DEVICE_MAX_PAGE				0xff
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

/*
 * msleep mode controls power management on the device and affects all
 * functions of the device.
 */
#define RMI_F01_CTRL0_SLEEP_MODE_MASK			0x03

#define RMI_SLEEP_MODE_NORMAL				0x00
#define RMI_SLEEP_MODE_SENSOR_SLEEP			0x01

/*
 * This bit disables whatever sleep mode may be selected by the sleep_mode
 * field and forces the device to run at full power without sleeping.
 */
#define RMI_F01_CRTL0_NOSLEEP_BIT			(1 << 2)

typedef struct
{
	FuSynapticsRmiFlash	 flash;
	GPtrArray		*functions;
	FuIOChannel		*io_channel;
	FuSynapticsRmiFunction	*f01;
	FuSynapticsRmiFunction	*f34;
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
	fu_common_string_append_kx (str, idt, "BlVer", priv->f34->function_version + 0x5);
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
fu_synaptics_rmi_device_read (FuSynapticsRmiDevice *self, guint16 addr, gsize req_sz, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* maximum size */
	if (req_sz > 0xffff) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "data to read was too long");
		return NULL;
	}

	/* report then old 1 byte read count */
	fu_byte_array_append_uint8 (req, RMI_READ_ADDR_REPORT_ID);
	fu_byte_array_append_uint8 (req, 0x0);

	/* address */
	fu_byte_array_append_uint16 (req, addr, G_LITTLE_ENDIAN);

	/* read output count */
	fu_byte_array_append_uint16 (req, req_sz, G_LITTLE_ENDIAN);

	/* request */
	for (guint j = req->len; j < 21; j++)
		fu_byte_array_append_uint8 (req, 0x0);
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "ReportWrite",
				     req->data, req->len,
				     80, FU_DUMP_FLAGS_NONE);
	}
	if (!fu_io_channel_write_byte_array (priv->io_channel, req, RMI_DEVICE_DEFAULT_TIMEOUT,
					     FU_IO_CHANNEL_FLAG_SINGLE_SHOT |
					     FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO, error))
		return NULL;

	/* keep reading responses until we get enough data */
	while (buf->len < req_sz) {
		guint8 input_count_sz = 0;
		g_autoptr(GByteArray) res = NULL;
		res = fu_io_channel_read_byte_array (priv->io_channel, req_sz,
						     RMI_DEVICE_DEFAULT_TIMEOUT,
						     FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						     error);
		if (res == NULL)
			return NULL;
		if (res->len == 0) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "response zero sized");
			return NULL;
		}
		if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
			fu_common_dump_full (G_LOG_DOMAIN, "ReportRead",
					     res->data, res->len,
					     80, FU_DUMP_FLAGS_NONE);
		}

		/* ignore non data report events */
		if (res->data[HID_RMI4_REPORT_ID] != RMI_READ_DATA_REPORT_ID) {
			g_debug ("ignoring report with ID 0x%02x",
				 res->data[HID_RMI4_REPORT_ID]);
			continue;
		}
		if (res->len < HID_RMI4_READ_INPUT_DATA) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "response too small: 0x%02x",
				     res->len);
			return NULL;
		}
		input_count_sz = res->data[HID_RMI4_READ_INPUT_COUNT];
		if (input_count_sz == 0) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "input count zero");
			return NULL;
		}
		if (input_count_sz + (guint) HID_RMI4_READ_INPUT_DATA > res->len) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "underflow 0x%02x from expected 0x%02x",
				     res->len, (guint) input_count_sz + HID_RMI4_READ_INPUT_DATA);
			return NULL;
		}
		g_byte_array_append (buf,
				     res->data + HID_RMI4_READ_INPUT_DATA,
				     input_count_sz);

	}
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "DeviceRead", buf->data, buf->len,
				     80, FU_DUMP_FLAGS_NONE);
	}

	return g_steal_pointer (&buf);
}

gboolean
fu_synaptics_rmi_device_write (FuSynapticsRmiDevice *self, guint16 addr, GByteArray *req, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint8 len = 0x0;
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	/* check size */
	if (req != NULL) {
		if (req->len > 0xff) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "data to write was too long");
			return FALSE;
		}
		len = req->len;
	}

	/* report */
	fu_byte_array_append_uint8 (buf, RMI_WRITE_REPORT_ID);

	/* length */
	fu_byte_array_append_uint8 (buf, len);

	/* address */
	fu_byte_array_append_uint16 (buf, addr, G_LITTLE_ENDIAN);

	/* optional data */
	if (req != NULL)
		g_byte_array_append (buf, req->data, req->len);

	/* pad out to 21 bytes for some reason */
	for (guint i = buf->len; i < 21; i++)
		fu_byte_array_append_uint8 (buf, 0x0);
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "DeviceWrite", buf->data, buf->len,
				     80, FU_DUMP_FLAGS_NONE);
	}

	return fu_io_channel_write_byte_array (priv->io_channel, buf, RMI_DEVICE_DEFAULT_TIMEOUT,
					       FU_IO_CHANNEL_FLAG_SINGLE_SHOT |
					       FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					       error);
}

static gboolean
fu_synaptics_rmi_device_set_rma_page (FuSynapticsRmiDevice *self, guint8 page, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fu_byte_array_append_uint8 (req, page);
	if (!fu_synaptics_rmi_device_write (self, RMI_DEVICE_PAGE_SELECT_REGISTER, req, error)) {
		g_prefix_error (error, "failed to set RMA page 0x%x", page);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_synaptics_rmi_device_reset (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) req = g_byte_array_new ();

	fu_byte_array_append_uint8 (req, RMI_F01_CMD_DEVICE_RESET);
	if (!fu_synaptics_rmi_device_write (self, priv->f01->command_base, req, error))
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
	for (guint page = 0; page < RMI_DEVICE_MAX_PAGE; page++) {
		gboolean found = FALSE;
		guint32 page_start = RMI_DEVICE_PAGE_SIZE * page;
		guint32 pdt_start = page_start + RMI_DEVICE_PAGE_SCAN_START;
		guint32 pdt_end = page_start + RMI_DEVICE_PAGE_SCAN_END;

		/* set page */
		if (!fu_synaptics_rmi_device_set_rma_page (self, page, error))
			return FALSE;

		/* read out functions */
		for (guint addr = pdt_start; addr >= pdt_end; addr -= RMI_DEVICE_PDT_ENTRY_SIZE) {
			g_autofree FuSynapticsRmiFunction *func = NULL;
			g_autoptr(GByteArray) res = NULL;
			res = fu_synaptics_rmi_device_read (self, addr, RMI_DEVICE_PDT_ENTRY_SIZE, error);
			if (res == NULL) {
				g_prefix_error (error, "failed to read PDT entry @ 0x%04x: ", addr);
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

typedef enum {
	HID_RMI4_MODE_MOUSE				= 0,
	HID_RMI4_MODE_ATTN_REPORTS			= 1,
	HID_RMI4_MODE_NO_PACKED_ATTN_REPORTS		= 2,
} FuSynapticsRmiHidMode;

static gboolean
fu_synaptics_rmi_device_set_mode (FuSynapticsRmiDevice *self,
				  FuSynapticsRmiHidMode mode,
				  GError **error)
{
	const guint8 data[] = { 0x0f, mode };
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "SetMode", data, sizeof(data));
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				     HIDIOCSFEATURE(sizeof(data)), (guint8 *) data,
				     NULL, error);
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
fu_synaptics_rmi_device_setup (FuDevice *device, GError **error)
{
	FuDeviceClass *klass_device = FU_DEVICE_GET_CLASS (device);
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_GET_CLASS (device);
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint16 addr;
	guint16 prod_info_addr;
	guint8 ds4_query_length = 0;
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

	/* read PDT */
	if (!fu_synaptics_rmi_device_scan_pdt (self, error))
		return FALSE;
	priv->f01 = fu_synaptics_rmi_device_get_function (self, 0x01, error);
	if (priv->f01 == NULL)
		return FALSE;
	addr = priv->f01->query_base;
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
	product_id = g_strndup ((const gchar *) f01_product_id->data, f01_product_id->len);
	if (product_id != NULL)
		fu_synaptics_rmi_device_set_product_id (self, product_id);

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
		priv->flash.build_id = fu_common_read_uint32 (buf32, G_LITTLE_ENDIAN);
	}

	priv->f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (priv->f34 == NULL)
		return FALSE;

	/* set up vfuncs for each bootloader protocol version */
	if (priv->f34->function_version == 0x0) {
		klass_rmi->setup = fu_synaptics_rmi_v5_device_setup;
		klass_rmi->query_status = fu_synaptics_rmi_v5_device_query_status;
		klass_device->detach = fu_synaptics_rmi_v5_device_detach;
		klass_device->write_firmware = fu_synaptics_rmi_v5_device_write_firmware;
	} else if (priv->f34->function_version == 0x1) {
		klass_rmi->setup = fu_synaptics_rmi_v6_device_setup;
		klass_rmi->query_status = fu_synaptics_rmi_v5_device_query_status;
		klass_device->detach = fu_synaptics_rmi_v5_device_detach;
		klass_device->write_firmware = fu_synaptics_rmi_v5_device_write_firmware;
	} else if (priv->f34->function_version == 0x2) {
		klass_rmi->setup = fu_synaptics_rmi_v7_device_setup;
		klass_rmi->query_status = fu_synaptics_rmi_v7_device_query_status;
		klass_device->detach = fu_synaptics_rmi_v7_device_detach;
		klass_device->write_firmware = fu_synaptics_rmi_v7_device_write_firmware;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "f34 function version 0x%02x unsupported",
			     priv->f34->function_version);
		return FALSE;
	}

	/* get Function34_Query0,1 */
	if (!klass_rmi->setup (self, error)) {
		g_prefix_error (error, "failed to read f34 queries: ");
		return FALSE;
	}
	if (!klass_rmi->query_status (self, error)) {
		g_prefix_error (error, "failed to read bootloader status: ");
		return FALSE;
	}

	/* set versions */
	fw_ver = g_strdup_printf ("%u.%u.%u",
				  f01_basic->data[2],
				  f01_basic->data[3],
				  priv->flash.build_id);
	fu_device_set_version (device, fw_ver, FWUPD_VERSION_FORMAT_TRIPLET);
	bl_ver = g_strdup_printf ("%u.0", priv->flash.bootloader_id[1]);
	fu_device_set_version_bootloader (device, bl_ver);

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_open (FuUdevDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);

	/* set up touchpad so we can query it */
	priv->io_channel = fu_io_channel_unix_new (fu_udev_device_get_fd (device));
	if (!fu_synaptics_rmi_device_set_mode (self, HID_RMI4_MODE_ATTN_REPORTS, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_close (FuUdevDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error_local = NULL;

	/* turn it back to mouse mode */
	if (!fu_synaptics_rmi_device_set_mode (self, HID_RMI4_MODE_MOUSE, &error_local)) {
		/* if just detached for replug, swallow error */
		if (!g_error_matches (error_local,
				      FWUPD_ERROR,
				      FWUPD_ERROR_PERMISSION_DENIED)) {
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		g_debug ("ignoring: %s", error_local->message);
	}

	fu_udev_device_set_fd (device, -1);
	g_clear_object (&priv->io_channel);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_probe (FuUdevDevice *device, GError **error)
{
	return fu_udev_device_set_physical_id (device, "hid", error);
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
	size_expected = (gsize) priv->flash.block_count_fw * (gsize) priv->flash.block_size;
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
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTimer) timer = g_timer_new ();

	/* wait for event from hardware */
	while (g_timer_elapsed (timer, NULL) * 1000.f < timeout_ms) {
		g_autoptr(GByteArray) res = NULL;
		g_autoptr(GError) error_local = NULL;

		/* read from fd */
		res = fu_io_channel_read_byte_array (priv->io_channel,
						     HID_RMI4_ATTN_INTERUPT_SOURCES + 1,
						     timeout_ms,
						     FU_IO_CHANNEL_FLAG_NONE,
						     &error_local);
		if (res == NULL) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT))
				break;
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
			fu_common_dump_full (G_LOG_DOMAIN, "ReportRead",
					     res->data, res->len,
					     80, FU_DUMP_FLAGS_NONE);
		}
		if (res->len < HID_RMI4_ATTN_INTERUPT_SOURCES + 1) {
			g_debug ("attr: ignoring small read of %u", res->len);
			continue;
		}
		if (res->data[HID_RMI4_REPORT_ID] != RMI_ATTN_REPORT_ID) {
			g_debug ("attr: ignoring invalid report ID 0x%x",
				 res->data[HID_RMI4_REPORT_ID]);
			continue;
		}

		/* success */
		if (source_mask & res->data[HID_RMI4_ATTN_INTERUPT_SOURCES])
			return TRUE;

		/* wrong mask */
		g_debug ("source mask did not match: 0x%x",
			 res->data[HID_RMI4_ATTN_INTERUPT_SOURCES]);
	}

	/* urgh */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no attr report, timed out");
	return FALSE;
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
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) f01_control0 = NULL;

	f01_control0 = fu_synaptics_rmi_device_read (self, priv->f01->control_base, 0x1, error);
	if (f01_control0 == NULL) {
		g_prefix_error (error, "failed to write get f01_control0: ");
		return FALSE;
	}
	f01_control0->data[0] |= RMI_F01_CRTL0_NOSLEEP_BIT;
	f01_control0->data[0] = (f01_control0->data[0] & ~RMI_F01_CTRL0_SLEEP_MODE_MASK) | RMI_SLEEP_MODE_NORMAL;
	if (!fu_synaptics_rmi_device_write (self,
					    priv->f01->control_base,
					    f01_control0,
					    error)) {
		g_prefix_error (error, "failed to write f01_control0: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_rmi_device_rebind_driver (FuSynapticsRmiDevice *self, GError **error)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (self));
	const gchar *hid_id;
	const gchar *driver;
	const gchar *subsystem;
	g_autofree gchar *fn_rebind = NULL;
	g_autofree gchar *fn_unbind = NULL;
	g_autoptr(GUdevDevice) parent_hid = NULL;
	g_autoptr(GUdevDevice) parent_i2c = NULL;

	/* get actual HID node */
	parent_hid = g_udev_device_get_parent_with_subsystem (udev_device, "hid", NULL);
	if (parent_hid == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no HID parent device for %s",
			     g_udev_device_get_sysfs_path (udev_device));
		return FALSE;
	}

	/* find the physical ID to use for the rebind */
	hid_id = g_udev_device_get_property (parent_hid, "HID_PHYS");
	if (hid_id == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no HID_PHYS in %s",
			     g_udev_device_get_sysfs_path (parent_hid));
		return FALSE;
	}
	g_debug ("HID_PHYS: %s", hid_id);

	/* build paths */
	parent_i2c = g_udev_device_get_parent_with_subsystem (udev_device, "i2c", NULL);
	if (parent_i2c == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no I2C parent device for %s",
			     g_udev_device_get_sysfs_path (udev_device));
		return FALSE;
	}
	driver = g_udev_device_get_driver (parent_i2c);
	subsystem = g_udev_device_get_subsystem (parent_i2c);
	fn_rebind = g_build_filename ("/sys/bus/", subsystem, "drivers", driver, "bind", NULL);
	fn_unbind = g_build_filename ("/sys/bus/", subsystem, "drivers", driver, "unbind", NULL);

	/* unbind hidraw, then bind it again to get a replug */
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	if (!fu_synaptics_rmi_device_writeln (fn_unbind, hid_id, error))
		return FALSE;
	if (!fu_synaptics_rmi_device_writeln (fn_rebind, hid_id, error))
		return FALSE;

	/* success */
	return TRUE;
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
					    bootloader_id_req, error)) {
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
					    error)) {
		g_prefix_error (error, "failed to disable interrupts: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_attach (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);

	/* reset device */
	if (!fu_synaptics_rmi_device_reset (self, error))
		return FALSE;

	/* rebind to rescan PDT with new firmware running */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	return fu_synaptics_rmi_device_rebind_driver (self, error);
}

static void
fu_synaptics_rmi_device_init (FuSynapticsRmiDevice *self)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.rmi");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_name (FU_DEVICE (self), "Touchpad");
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
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
	FuUdevDeviceClass *klass_device_udev = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_synaptics_rmi_device_finalize;
	klass_device->to_string = fu_synaptics_rmi_device_to_string;
	klass_device->prepare_firmware = fu_synaptics_rmi_device_prepare_firmware;
	klass_device->attach = fu_synaptics_rmi_device_attach;
	klass_device->setup = fu_synaptics_rmi_device_setup;
	klass_device_udev->probe = fu_synaptics_rmi_device_probe;
	klass_device_udev->open = fu_synaptics_rmi_device_open;
	klass_device_udev->close = fu_synaptics_rmi_device_close;
}
