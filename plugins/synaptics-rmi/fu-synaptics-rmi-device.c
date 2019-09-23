/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2014 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include <glib/gstdio.h>

#include "fu-common.h"
#include "fu-chunk.h"
#include "fu-io-channel.h"

#include "fu-synaptics-rmi-common.h"
#include "fu-synaptics-rmi-device.h"
#include "fu-synaptics-rmi-firmware.h"

#define RMI_WRITE_REPORT_ID			0x9 // Output Report
#define RMI_READ_ADDR_REPORT_ID			0xa // Output Report
#define RMI_READ_DATA_REPORT_ID			0xb // Input Report
#define RMI_ATTN_REPORT_ID			0xc // Input Report
#define RMI_SET_RMI_MODE_REPORT_ID		0xf // Feature Report

#define HID_RMI4_REPORT_ID			0
#define HID_RMI4_READ_INPUT_COUNT		1
#define HID_RMI4_READ_INPUT_DATA		2
#define HID_RMI4_READ_OUTPUT_ADDR		2
#define HID_RMI4_READ_OUTPUT_COUNT		4
#define HID_RMI4_WRITE_OUTPUT_COUNT		1
#define HID_RMI4_WRITE_OUTPUT_ADDR		2
#define HID_RMI4_WRITE_OUTPUT_DATA		4
#define HID_RMI4_FEATURE_MODE			1
#define HID_RMI4_ATTN_INTERUPT_SOURCES		1
#define HID_RMI4_ATTN_DATA			2

typedef struct
{
	gint			 fd;
	guint8			 bootloader_id[2];
	guint16			 block_size;
	guint16			 block_count_fw;
	guint16			 block_count_cfg;
	GPtrArray		*functions;
	FuIOChannel		*io_channel;
	FuSynapticsRmiFunction	*f01;
	FuSynapticsRmiFunction	*f34;
	guint8			 page;
	guint			 num_interrupt_regs;
	guint8			 manufacturer_id;
	guint8			 has_lts;
	guint8			 has_sensor_id;
	guint8			 has_query42;
	guint8			 has_dds4_queries;
	guint8			 f34_status_addr;
	guint32			 config_id;
	guint8			 sensor_id;
	guint16			 package_id;
	guint16			 package_rev;
	guint16			 build_id;
} FuSynapticsRmiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuSynapticsRmiDevice, fu_synaptics_rmi_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_synaptics_rmi_device_get_instance_private (o))

static void
fu_synaptics_rmi_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_ku (str, idt, "FD", (guint) priv->fd);
	if (priv->bootloader_id[0] != 0x0) {
		g_autofree gchar *tmp = g_strdup_printf ("%02x.%02x",
							 priv->bootloader_id[0],
							 priv->bootloader_id[1]);
		fu_common_string_append_kv (str, idt, "BootloaderId", tmp);
	}
	fu_common_string_append_kx (str, idt, "BlockSize", priv->block_size);
	fu_common_string_append_kx (str, idt, "BlockCountFw", priv->block_count_fw);
	fu_common_string_append_kx (str, idt, "BlockCountCfg", priv->block_count_cfg);
	fu_common_string_append_kx (str, idt, "ManufacturerID", priv->manufacturer_id);
	fu_common_string_append_kb (str, idt, "HasLts", priv->has_lts);
	fu_common_string_append_kb (str, idt, "HasSensorID", priv->has_sensor_id);
	fu_common_string_append_kb (str, idt, "HasQuery42", priv->has_query42);
	fu_common_string_append_kb (str, idt, "HasDS4Queries", priv->has_dds4_queries);
	fu_common_string_append_kx (str, idt, "ConfigID", priv->config_id);
	fu_common_string_append_kx (str, idt, "PackageID", priv->package_id);
	fu_common_string_append_kx (str, idt, "PackageRev", priv->package_rev);
	fu_common_string_append_kx (str, idt, "BuildID", priv->build_id);
	fu_common_string_append_kx (str, idt, "SensorID", priv->sensor_id);
}

static FuSynapticsRmiFunction *
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

static GByteArray *
fu_synaptics_rmi_device_read (FuSynapticsRmiDevice *self, guint16 addr, gsize req_sz, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint8 input_count_sz;
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	/* maximum size */
	if (req_sz > 0xffff) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "data to read was too long");
		return NULL;
	}

	/* weirdly, request a word of data, then increment in a byte-sized section */
	for (guint i = 0; i < req_sz; i += input_count_sz) {
		g_autoptr(GByteArray) req = g_byte_array_new ();
		g_autoptr(GByteArray) res = NULL;

		/* report */
		fu_byte_array_append_uint8 (req, RMI_READ_ADDR_REPORT_ID);

		/* old 1 byte read count */
		fu_byte_array_append_uint8 (req, 0x0);

		/* address */
		fu_byte_array_append_uint16 (req, addr + i, G_LITTLE_ENDIAN);

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

		/* response */
		res = fu_io_channel_read_byte_array (priv->io_channel, req_sz, RMI_DEVICE_DEFAULT_TIMEOUT,
						     FU_IO_CHANNEL_FLAG_NONE, error);
		if (res == NULL)
			return NULL;
		if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
			fu_common_dump_full (G_LOG_DOMAIN, "ReportRead",
					     res->data, res->len,
					     80, FU_DUMP_FLAGS_NONE);
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
		if (input_count_sz < (guint) req_sz) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "input count smaller 0x%02x than request 0x%02x",
				     input_count_sz, (guint) req_sz);
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

static gboolean
fu_synaptics_rmi_device_write (FuSynapticsRmiDevice *self, guint16 addr, GByteArray *req, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint8 addr_le[2] = { 0x0 };
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
	g_byte_array_append (buf, &len, sizeof(len));

	/* address */
	fu_common_write_uint16 (addr_le, addr, G_LITTLE_ENDIAN);
	g_byte_array_append (buf, addr_le, sizeof(addr_le));

	/* optional data */
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
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* same */
	if (priv->page == page)
		return TRUE;

	/* write */
	fu_byte_array_append_uint8 (req, page);
	if (!fu_synaptics_rmi_device_write (self, RMI_DEVICE_PAGE_SELECT_REGISTER, req, error)) {
		priv->page = -1;
		return FALSE;
	}
	priv->page = page;
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_reset (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) req = g_byte_array_new ();

	g_debug ("resetting...");
	fu_byte_array_append_uint8 (req, RMI_F01_CMD_DEVICE_RESET);
	if (!fu_synaptics_rmi_device_write (self, priv->f01->command_base, req, error))
		return FALSE;
	g_usleep (1000 * RMI_F01_DEFAULT_RESET_DELAY_MS);
	g_debug ("reset completed");
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
		guint page_start = RMI_DEVICE_PAGE_SIZE * page;
		guint pdt_start = page_start + RMI_DEVICE_PAGE_SCAN_START;
		guint pdt_end = page_start + RMI_DEVICE_PAGE_SCAN_END;

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
		}
	}

	/* see docs */
	priv->num_interrupt_regs = (interrupt_count + 7) / 8;

	/* for debug */
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

	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_set_mode (FuSynapticsRmiDevice *self,
				  FuSynapticsRmiHidMode mode,
				  GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	const guint8 data[] = { 0x0f, mode };

	fu_common_dump_raw (G_LOG_DOMAIN, "SetMode", data, sizeof(data));
	if (ioctl (priv->fd, HIDIOCSFEATURE(sizeof(data)), data) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to SetMode");
		return FALSE;
	}
	return TRUE;
}

#if 0
static gboolean
fu_synaptics_rmi_device_set_feature (FuSynapticsRmiDevice *self,
				     const guint8 *data,
				     guint datasz,
				     GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);

	/* Set Feature */
	fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", data, datasz);
	if (ioctl (priv->fd, HIDIOCSFEATURE(datasz), data) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to SetFeature");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_get_feature (FuSynapticsRmiDevice *self,
				     guint8 *data,
				     guint datasz,
				     GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	if (ioctl (priv->fd, HIDIOCGFEATURE(datasz), data) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to GetFeature");
		return FALSE;
	}
	fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", data, datasz);
	return TRUE;
}
#endif

static gboolean
fu_synaptics_rmi_device_read_flash_config (FuSynapticsRmiDevice *self, GError **error)
{
	//FIXME:
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_read_f34_queries_v7 (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint8 offset;
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_dataX = NULL;

	f34_data0 = fu_synaptics_rmi_device_read (self, priv->f34->query_base, 1, error);
	if (f34_data0 == NULL) {
		g_prefix_error (error, "failed to read bootloader ID: ");
		return FALSE;
	}
	offset = (f34_data0->data[0] & 0b00000111) + 1;
//	priv->has_config_id = f34_data0->data & 0b00001000;
	f34_dataX = fu_synaptics_rmi_device_read (self, priv->f34->query_base + offset, 21, error);
	if (f34_dataX == NULL)
		return FALSE;
	priv->bootloader_id[0] = f34_dataX->data[0x00];
	priv->bootloader_id[1] = f34_dataX->data[0x01];
	priv->block_size = fu_common_read_uint16 (f34_dataX->data + 0x07, G_LITTLE_ENDIAN);
	//priv->flash_config_length = fu_common_read_uint16 (f34_dataX->data + 0x0d, G_LITTLE_ENDIAN);
	//priv->payload_length = fu_common_read_uint16 (f34_dataX->data + 0x0f, G_LITTLE_ENDIAN);
	priv->build_id = fu_common_read_uint32 (f34_dataX->data + 0x02, G_LITTLE_ENDIAN);
	return fu_synaptics_rmi_device_read_flash_config (self, error);
}

static gboolean
fu_synaptics_rmi_device_read_f34_queries_v1 (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_data1 = NULL;
	g_autoptr(GByteArray) f34_data2 = NULL;
	g_autoptr(GByteArray) f34_data3 = NULL;

	/* get bootloader ID */
	f34_data0 = fu_synaptics_rmi_device_read (self,
						  priv->f34->query_base,
						  RMI_BOOTLOADER_ID_SIZE,
						  error);
	if (f34_data0 == NULL) {
		g_prefix_error (error, "failed to read bootloader ID: ");
		return FALSE;
	}
	priv->bootloader_id[0] = f34_data0->data[0];
	priv->bootloader_id[1] = f34_data0->data[1];

	/* get flash properties */
	f34_data1 = fu_synaptics_rmi_device_read (self, priv->f34->query_base + 0x01, 1, error);
	if (f34_data1 == NULL)
		return FALSE;
//	priv->has_new_regmap = f34_data1->data[0] & RMI_F34_HAS_NEW_REG_MAP;
//	priv->has_config_id = f34_data1->data[0] & RMI_F34_HAS_CONFIG_ID;
	f34_data2 = fu_synaptics_rmi_device_read (self, priv->f34->query_base + 0x02, 2, error);
	if (f34_data2 == NULL)
		return FALSE;
	priv->block_size = fu_common_read_uint16 (f34_data2->data + RMI_F34_BLOCK_SIZE_V1_OFFSET, G_LITTLE_ENDIAN);
	f34_data3 = fu_synaptics_rmi_device_read (self, priv->f34->query_base + 0x03, 8, error);
	if (f34_data3 == NULL)
		return FALSE;
	priv->block_count_fw = fu_common_read_uint16 (f34_data3->data + RMI_F34_FW_BLOCKS_V1_OFFSET, G_LITTLE_ENDIAN);
	priv->block_count_cfg = fu_common_read_uint16 (f34_data3->data + RMI_F34_CONFIG_BLOCKS_V1_OFFSET, G_LITTLE_ENDIAN);
	priv->f34_status_addr = priv->f34->data_base + 2;
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_read_f34_queries_v0 (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_data2 = NULL;

	/* get bootloader ID */
	f34_data0 = fu_synaptics_rmi_device_read (self,
						  priv->f34->query_base,
						  RMI_BOOTLOADER_ID_SIZE,
						  error);
	if (f34_data0 == NULL) {
		g_prefix_error (error, "failed to read bootloader ID: ");
		return FALSE;
	}
	priv->bootloader_id[0] = f34_data0->data[0];
	priv->bootloader_id[1] = f34_data0->data[1];

	/* get flash properties */
	f34_data2 = fu_synaptics_rmi_device_read (self,
						  priv->f34->query_base + 0x2,
						  RMI_F34_QUERY_SIZE,
						  error);
	if (f34_data2 == NULL)
		return FALSE;
//	priv->has_new_regmap = f34_data2->data[0] & RMI_F34_HAS_NEW_REG_MAP;
//	priv->has_config_id = f34_data2->data[0] & RMI_F34_HAS_CONFIG_ID;
	priv->block_size = fu_common_read_uint16 (f34_data2->data + RMI_F34_BLOCK_SIZE_OFFSET, G_LITTLE_ENDIAN);
	priv->block_count_fw = fu_common_read_uint16 (f34_data2->data + RMI_F34_FW_BLOCKS_OFFSET, G_LITTLE_ENDIAN);
	priv->block_count_cfg = fu_common_read_uint16 (f34_data2->data + RMI_F34_CONFIG_BLOCKS_OFFSET, G_LITTLE_ENDIAN);
	priv->f34_status_addr = priv->f34->data_base + RMI_F34_BLOCK_DATA_OFFSET + priv->block_size;
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_setup (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	guint16 addr;
	guint16 prod_info_addr;
	guint8 ds4_query_length = 0;
	guint8 has_build_id_query = FALSE;
	guint8 has_package_id_query = FALSE;
	g_autofree gchar *bl_ver = NULL;
	g_autofree gchar *fw_ver = NULL;
	g_autofree gchar *name = NULL;
	g_autoptr(GByteArray) f01_basic = NULL;
	g_autoptr(GByteArray) f01_product_id = NULL;
	g_autoptr(GByteArray) f34_ctrl = NULL;

	/* read basic device information */
	if (!fu_synaptics_rmi_device_set_rma_page (self, 0x00, error))
		return FALSE;

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
	priv->manufacturer_id = f01_basic->data[0]; // FIXME: vendor_id?
	priv->has_lts = f01_basic->data[1] & RMI_DEVICE_F01_QRY1_HAS_LTS;
	priv->has_sensor_id = f01_basic->data[1] & RMI_DEVICE_F01_QRY1_HAS_SENSOR_ID;
	priv->has_query42 = f01_basic->data[1] & RMI_DEVICE_F01_QRY1_HAS_PROPS_2;
	fw_ver = g_strdup_printf ("%u.%u", f01_basic->data[2], f01_basic->data[3]);
	fu_device_set_version (device, fw_ver, FWUPD_VERSION_FORMAT_PAIR);

	/* use the product ID as the name */
	addr += 11;
	f01_product_id = fu_synaptics_rmi_device_read (self, addr, RMI_PRODUCT_ID_LENGTH, error);
	if (f01_product_id == NULL) {
		g_prefix_error (error, "failed to read the product id: ");
		return FALSE;
	}
	name = g_strndup ((const gchar *) f01_product_id->data, f01_product_id->len);
	fu_device_set_name (device, name);

	/* skip */
	prod_info_addr = addr + 6;
	addr += 10;
	if (priv->has_lts)
		addr++;

	/* get sensor ID */
	if (priv->has_sensor_id) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, addr++, 1, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read the sensor id: ");
			return FALSE;
		}
		priv->sensor_id = f01_tmp->data[0];
	}

	/* skip */
	if (priv->has_lts)
		addr += RMI_DEVICE_F01_LTS_RESERVED_SIZE;

	/* read package ids */
	if (priv->has_query42) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, addr++, 1, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read query 42: ");
			return FALSE;
		}
		priv->has_dds4_queries = f01_tmp->data[0] & RMI_DEVICE_F01_QRY42_DS4_QUERIES;
	}
	if (priv->has_dds4_queries) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, addr++, 1, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read DS4 query length: ");
			return FALSE;
		}
		ds4_query_length = f01_tmp->data[0];
	}
	for (guint i = 1; i <= ds4_query_length; ++i) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, addr++, 1, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read F01 Query43.%02x: ", i);
			return FALSE;
		}
		switch (i) {
		case 1:
			has_package_id_query = f01_tmp->data[0] & RMI_DEVICE_F01_QRY43_01_PACKAGE_ID;
			has_build_id_query = f01_tmp->data[0] & RMI_DEVICE_F01_QRY43_01_BUILD_ID;
			break;
		default:
			break;
		}
	}
	if (has_package_id_query) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, prod_info_addr++, PACKAGE_ID_BYTES, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read package id: ");
			return FALSE;
		}
		priv->package_id = fu_common_read_uint16 (f01_tmp->data, G_LITTLE_ENDIAN);
		priv->package_rev = fu_common_read_uint16 (f01_tmp->data + 2, G_LITTLE_ENDIAN);
	}
	if (has_build_id_query) {
		g_autoptr(GByteArray) f01_tmp = NULL;
		f01_tmp = fu_synaptics_rmi_device_read (self, prod_info_addr, BUILD_ID_BYTES, error);
		if (f01_tmp == NULL) {
			g_prefix_error (error, "failed to read build ID bytes: ");
			return FALSE;
		}
		priv->build_id = fu_common_read_uint16 (f01_tmp->data, G_LITTLE_ENDIAN);
	}

	priv->f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (priv->f34 == NULL)
		return FALSE;
	f34_ctrl = fu_synaptics_rmi_device_read (self, priv->f34->control_base, CONFIG_ID_BYTES, error);
	if (f34_ctrl == NULL) {
		g_prefix_error (error, "failed to read the config id: ");
		return FALSE;
	}
	priv->config_id = fu_common_read_uint32 (f34_ctrl->data, G_LITTLE_ENDIAN);

	/* get Function34_Query0,1 */
	if (priv->f34->function_version == 0x0) {
		if (!fu_synaptics_rmi_device_read_f34_queries_v0 (self, error)) {
			g_prefix_error (error, "failed to read f34 queries: ");
			return FALSE;
		}
	} else if (priv->f34->function_version == 0x1) {
		if (!fu_synaptics_rmi_device_read_f34_queries_v1 (self, error)) {
			g_prefix_error (error, "failed to read f34 queries: ");
			return FALSE;
		}
	} else if (priv->f34->function_version == 0x2) {
		if (!fu_synaptics_rmi_device_read_f34_queries_v7 (self, error)) {
			g_prefix_error (error, "failed to read f34 queries: ");
			return FALSE;
		}
	}
	bl_ver = g_strdup_printf ("%u.0", priv->bootloader_id[1]);
	fu_device_set_version_bootloader (device, bl_ver);

	/* get Function34:FlashProgrammingEn */
	if (priv->bootloader_id[0] & 0x40) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_open (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (device));

	/* open device */
	priv->fd = g_open (g_udev_device_get_device_file (udev_device), O_RDWR);
	if (priv->fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to open %s",
			     g_udev_device_get_device_file (udev_device));
		return FALSE;
	}
	priv->io_channel = fu_io_channel_unix_new (priv->fd);

	/* set up touchpad so we can query it */
	if (!fu_synaptics_rmi_device_set_mode (self, HID_RMI4_MODE_ATTN_REPORTS, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_close (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);

	/* turn it back to mouse mode */
	if (!fu_synaptics_rmi_device_set_mode (self, HID_RMI4_MODE_MOUSE, error))
		return FALSE;

	g_clear_object (&priv->io_channel);
	priv->fd = 0;
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

	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check sizes */
	bytes_bin = fu_firmware_get_image_by_id_bytes (firmware, "ui", error);
	if (bytes_bin == NULL)
		return FALSE;
	if (g_bytes_get_size (bytes_bin) != priv->block_count_fw * priv->block_size) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "file firmware invalid size 0x%04x",
			     (guint) g_bytes_get_size (bytes_bin));
		return FALSE;
	}
	bytes_cfg = fu_firmware_get_image_by_id_bytes (firmware, "config", error);
	if (bytes_cfg == NULL)
		return FALSE;
	if (g_bytes_get_size (bytes_cfg) != priv->block_count_cfg * priv->block_size) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "file config invalid size 0x%04x",
			     (guint) g_bytes_get_size (bytes_cfg));
		return FALSE;
	}

	return g_steal_pointer (&firmware);
}


static gboolean
fu_synaptics_rmi_device_write_block (FuSynapticsRmiDevice *self,
				     guint8 cmd,
				     guint32 idx,
				     guint32 address,
				     const guint8 *data,
				     guint16 datasz,
				     GError **error)
{
//	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);

	//FIXME: Write @address tp F34_Flash_Data0,1, but after first chunk it auto-increments...
	if (idx == 0) {
		//FIXME
	}
	//FIXME: write @data to F34_Flash_Data2
	//FIXME: write @cmd into F34_Flash_Data3
	//FIXME: wait for ATTN and check success $80
	return TRUE;
}

static gboolean
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

static gboolean
fu_synaptics_rmi_device_write_firmware (FuDevice *device,
					FuFirmware *firmware,
					FwupdInstallFlags flags,
					GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GBytes) bytes_bin = NULL;
	g_autoptr(GBytes) bytes_cfg = NULL;
	g_autoptr(GPtrArray) chunks_bin = NULL;
	g_autoptr(GPtrArray) chunks_cfg = NULL;

	/* get both images */
	bytes_bin = fu_firmware_get_image_by_id_bytes (firmware, "ui", error);
	if (bytes_bin == NULL)
		return FALSE;
	bytes_cfg = fu_firmware_get_image_by_id_bytes (firmware, "config", error);
	if (bytes_cfg == NULL)
		return FALSE;

	/* build packets */
	chunks_bin = fu_chunk_array_new_from_bytes (bytes_bin,
						    0x00,	/* start addr */
						    0x00,	/* page_sz */
						    priv->block_size);
	chunks_cfg = fu_chunk_array_new_from_bytes (bytes_cfg,
						    0x00,	/* start addr */
						    0x00,	/* page_sz */
						    priv->block_size);

	/* disable powersaving */
	if (!fu_synaptics_rmi_device_disable_sleep (self, error))
		return FALSE;

	/* erase all */
	//FIXME: write $3 into F34_Flash_Data3
	//FIXME: wait for ATTN and check success
	/* v7+ */
	if (priv->f34->function_version == 0x02) {
		g_autoptr(GByteArray) erase_cmd = g_byte_array_new ();
		fu_byte_array_append_uint8 (erase_cmd, CORE_CODE_PARTITION);
		fu_byte_array_append_uint32 (erase_cmd, 0x0, G_LITTLE_ENDIAN);
		if (priv->bootloader_id[1] == 8){
			// For bootloader v8
			fu_byte_array_append_uint8 (erase_cmd, CMD_V7_ERASE_AP);
		} else {
			// For bootloader v7
			fu_byte_array_append_uint8 (erase_cmd, CMD_V7_ERASE);
		} 
		fu_byte_array_append_uint8 (erase_cmd, priv->bootloader_id[0]);
		fu_byte_array_append_uint8 (erase_cmd, priv->bootloader_id[1]);
		//FIXME: rmi4update_poll()
		//FIXME: Check whether device is in bootloader mode (m_inBLmode)
		if (priv->bootloader_id[1] == 8){
			g_usleep(1000);
		}
		if (!fu_synaptics_rmi_device_write (self,
						    priv->f34->data_base + 1,
						    erase_cmd,
						    error)) {
			g_prefix_error (error, "failed to enable programming: ");
			return FALSE;
		}
		//FIXME: wait for ATTN and check success

		if (priv->bootloader_id[1] == 7) {
			//FIXME: rmi4update_poll()
			//FIXME: Check whether device is in bootloader mode (m_inBLmode)
			g_autoptr(GByteArray) eraseConfigv7_cmd = g_byte_array_new ();
			fu_byte_array_append_uint8 (eraseConfigv7_cmd, CORE_CONFIG_PARTITION);
			fu_byte_array_append_uint32 (eraseConfigv7_cmd, 0x0, G_LITTLE_ENDIAN);
			fu_byte_array_append_uint8 (eraseConfigv7_cmd, CMD_V7_ERASE);
			g_usleep(100);
			if (!fu_synaptics_rmi_device_write (self,
							priv->f34->data_base + 1,
						    eraseConfigv7_cmd,
						    error)) {
				g_prefix_error (error, "failed to enable programming: ");
				return FALSE;
			}
			//FIXME: wait RMI_F34_ENABLE_WAIT_MS for ATTN 
		}
	}

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks_bin->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks_bin, i);
		if (!fu_synaptics_rmi_device_write_block (self,
							  0x02, /* write firmware */
							  chk->idx,
							  chk->address,
							  chk->data,
							  chk->data_sz,
							  error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i,
					     (gsize) chunks_bin->len + chunks_cfg->len);
	}

	/* program the configuration image */
	for (guint i = 0; i < chunks_cfg->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks_cfg, i);
		if (!fu_synaptics_rmi_device_write_block (self,
							  0x06, /* write firmware */
							  chk->idx,
							  chk->address,
							  chk->data,
							  chk->data_sz,
							  error))
			return FALSE;
		fu_device_set_progress_full (device,
					     (gsize) chunks_bin->len + i,
					     (gsize) chunks_bin->len + chunks_cfg->len);
	}

	//FIXME
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_rebind_driver (FuSynapticsRmiDevice *self, GError **error)
{
	const gchar *sysfs_path = fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (self));
	const guint8 driver[] = "hidraw";
	g_autofree gchar *fn_rebind = g_build_filename (sysfs_path, "bind", NULL);
	g_autofree gchar *fn_unbind = g_build_filename (sysfs_path, "unbind", NULL);
	g_autoptr(FuIOChannel) io_rebind = NULL;
	g_autoptr(FuIOChannel) io_unbind = NULL;
	g_autoptr(GByteArray) req = g_byte_array_new ();

	/* unbind hidraw, then bind it again to get a replug */
	g_byte_array_append (req, driver, sizeof (driver));
	io_unbind = fu_io_channel_new_file (fn_unbind, error);
	if (io_rebind == NULL)
		return FALSE;
	if (fu_io_channel_write_byte_array (io_unbind,
					    req,
					    RMI_DEVICE_DEFAULT_TIMEOUT,
					    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					    error))
		return FALSE;
	io_rebind = fu_io_channel_new_file (fn_rebind, error);
	if (io_rebind == NULL)
		return FALSE;
	if (fu_io_channel_write_byte_array (io_rebind,
					    req,
					    RMI_DEVICE_DEFAULT_TIMEOUT,
					    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_write_bootloader_id (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	gint block_data_offset = RMI_F34_BLOCK_DATA_OFFSET;
	g_autoptr(GByteArray) bootloader_id_req = g_byte_array_new ();

	if (priv->f34->function_version == 0x1)
		block_data_offset = RMI_F34_BLOCK_DATA_V1_OFFSET;

	/* write bootloader_id into F34_Flash_Data0,1 */
	g_byte_array_append (bootloader_id_req, priv->bootloader_id, sizeof(priv->bootloader_id));
	if (!fu_synaptics_rmi_device_write (self,
					    priv->f34->data_base + block_data_offset,
					    bootloader_id_req, error)) {
		g_prefix_error (error, "failed to write bootloader_id: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_detach (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GByteArray) interrupt_disable_req = g_byte_array_new ();
	g_autoptr(GByteArray) enable_req = g_byte_array_new ();

	/* disable interrupts */
	fu_byte_array_append_uint8 (interrupt_disable_req,
				    priv->f34->interrupt_mask | priv->f01->interrupt_mask);
	if (!fu_synaptics_rmi_device_write (self,
					    priv->f01->control_base + 1,
					    interrupt_disable_req,
					    error)) {
		g_prefix_error (error, "failed to disable interrupts: ");
		return FALSE;
	}

	/* v7 */
	if (priv->f34->function_version == 0x02) {
		fu_byte_array_append_uint8 (enable_req, BOOTLOADER_PARTITION);
		fu_byte_array_append_uint32 (enable_req, 0x0, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint8 (enable_req, CMD_V7_ENTER_BL);
		fu_byte_array_append_uint8 (enable_req, priv->bootloader_id[0]);
		fu_byte_array_append_uint8 (enable_req, priv->bootloader_id[1]);
		if (!fu_synaptics_rmi_device_write (self,
						    priv->f34->data_base + 1,
						    enable_req,
						    error)) {
			g_prefix_error (error, "failed to enable programming: ");
			return FALSE;
		}
	} else {
		/* unlock bootloader and rebind kernel driver */
		if (!fu_synaptics_rmi_device_write_bootloader_id (self, error))
			return FALSE;
		fu_byte_array_append_uint8 (enable_req, RMI_F34_ENABLE_FLASH_PROG);
		if (!fu_synaptics_rmi_device_write (self,
						    priv->f34_status_addr,
						    enable_req,
						    error)) {
			g_prefix_error (error, "failed to enable programming: ");
			return FALSE;
		}
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	g_usleep (1000 * RMI_F34_ENABLE_WAIT_MS);
	return fu_synaptics_rmi_device_rebind_driver (self, error);
}

static gboolean
fu_synaptics_rmi_device_attach (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);

	/* reset device */
	if (!fu_synaptics_rmi_device_reset (self, error))
		return FALSE;

	//FIXME: wait for ATTN, or just usleep...

	/* rescan PDT */
	return fu_synaptics_rmi_device_scan_pdt (self, error);
}

static void
fu_synaptics_rmi_device_init (FuSynapticsRmiDevice *self)
{
	FuSynapticsRmiDevicePrivate *priv = GET_PRIVATE (self);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	priv->functions = g_ptr_array_new_with_free_func (g_free);
	priv->page = 0xff;
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
	klass_device->open = fu_synaptics_rmi_device_open;
	klass_device->close = fu_synaptics_rmi_device_close;
	klass_device->prepare_firmware = fu_synaptics_rmi_device_prepare_firmware;
	klass_device->write_firmware = fu_synaptics_rmi_device_write_firmware;
	klass_device->attach = fu_synaptics_rmi_device_attach;
	klass_device->detach = fu_synaptics_rmi_device_detach;
	klass_device->setup = fu_synaptics_rmi_device_setup;
	klass_device_udev->probe = fu_synaptics_rmi_device_probe;
}

FuSynapticsRmiDevice *
fu_synaptics_rmi_device_new (FuUdevDevice *device)
{
	FuSynapticsRmiDevice *self = NULL;
	self = g_object_new (FU_TYPE_SYNAPTICS_RMI_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}
