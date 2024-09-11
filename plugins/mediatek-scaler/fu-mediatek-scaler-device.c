/*
 * Copyright (C) 2023 Dell Technologies
 * Copyright (C) 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <linux/i2c-dev.h>

#include "fu-mediatek-scaler-common.h"
#include "fu-mediatek-scaler-device.h"
#include "fu-mediatek-scaler-firmware.h"
#include "fu-mediatek-scaler-struct.h"

#define DDC_DATA_LEN_DFT	0x80
#define DDC_DATA_FRAGEMENT_SIZE 0x0B   /* 11 bytes for each DDC write */
#define DDC_DATA_PAGE_SIZE	0x1000 /* 4K bytes for each block page */
#define DDC_RW_MAX_RETRY_CNT	10

/* supported display controller type */
#define FU_MEDIATEK_SCALER_SUPPORTED_CONTROLLER_TYPE 0x00005605

/* timeout duration in ms to i2c-dev operation */
#define FU_MEDIATEK_SCALER_DEVICE_IOCTL_TIMEOUT 5000

/* delay time before a ddc read or write */
#define FU_MEDIATEK_SCALER_DDC_MSG_DELAY_MS 50

/* delay time before a ddc read or write */
#define FU_MEDIATEK_SCALER_CHUNK_SENT_DELAY_MS 1

/* interval in ms between the poll to check device status */
#define FU_MEDIATEK_SCALER_DEVICE_POLL_INTERVAL 1000

/* maximum retries for polliing the device existence */
#define FU_MEDIATEK_SCALER_DEVICE_PRESENT_RETRY 100

/* firmware payload size */
#define FU_MEDIATEK_SCALER_FW_SIZE_MAX 0x100000

/**
 * FU_MEDIATEK_SCALER_DEVICE_FLAG_PROBE_VCP:
 *
 * Device VCP should be probed.
 */
#define FU_MEDIATEK_SCALER_DEVICE_FLAG_PROBE_VCP (1 << 0)

/**
 * FWUPD_MEDIATEK_SCALER_FLAG_BANK2_ONLY:
 *
 * Install firmware to bank 2 only.
 */
#define FWUPD_MEDIATEK_SCALER_FLAG_BANK2_ONLY (1 << 1)

typedef struct {
	FuChunk *chk;
	guint32 sent_sz;
} FuMediatekScalerWriteChunkHelper;

struct _FuMediatekScalerDevice {
	FuUdevDevice parent_instance;
	FuUdevDevice *i2c_dev;
};

G_DEFINE_TYPE(FuMediatekScalerDevice, fu_mediatek_scaler_device, FU_TYPE_UDEV_DEVICE)

static void
fu_mediatek_scaler_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_mediatek_scaler_device_parent_class)->to_string(device, idt, str);

	if (self->i2c_dev != NULL) {
		fu_string_append(str,
				 idt,
				 "I2cDeviceFile",
				 fu_udev_device_get_device_file(FU_UDEV_DEVICE(self->i2c_dev)));
	}
}

static gboolean
fu_mediatek_scaler_ensure_device_address(FuMediatekScalerDevice *self,
					 guint8 address,
					 GError **error)
{
	if (!fu_udev_device_ioctl(self->i2c_dev,
				  I2C_SLAVE,
				  (guint8 *)(guintptr)address,
				  NULL,
				  FU_MEDIATEK_SCALER_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		g_prefix_error(error,
			       "failed to set address '0x%02x' on %s: ",
			       address,
			       fu_udev_device_get_device_file(FU_UDEV_DEVICE(self->i2c_dev)));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_set_i2c_dev(FuMediatekScalerDevice *self,
				      const GPtrArray *devices,
				      GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuUdevDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) i2c_devs =
		    fu_udev_device_get_children_with_subsystem(device, "i2c-dev");

		if (i2c_devs->len == 0) {
			g_debug("no i2c-dev found under %s", fu_udev_device_get_sysfs_path(device));
			continue;
		}
		if (i2c_devs->len > 1) {
			g_debug("ignoring %u additional i2c-dev under %s",
				i2c_devs->len - 1,
				fu_udev_device_get_sysfs_path(device));
		}

		/* the first i2c_dev is enforced to represent the dp aux device */
		self->i2c_dev = g_object_ref(g_ptr_array_index(i2c_devs, 0));
		g_debug("found I2C bus at %s, using this device",
			fu_udev_device_get_sysfs_path(self->i2c_dev));
		return fu_udev_device_set_physical_id(self->i2c_dev, "i2c", error);
	}
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no devices on the i2c bus");
	return FALSE;
}

static gboolean
fu_mediatek_scaler_device_use_aux_dev(FuMediatekScalerDevice *self, GError **error)
{
	g_autoptr(GPtrArray) i2c_devices =
	    fu_udev_device_get_siblings_with_subsystem(FU_UDEV_DEVICE(self), "i2c");
	return fu_mediatek_scaler_device_set_i2c_dev(self, i2c_devices, error);
}

static gboolean
fu_mediatek_scaler_device_ddc_write(FuMediatekScalerDevice *self,
				    GByteArray *st_req,
				    GError **error)
{
	guint8 chksum = 0;
	g_autoptr(GByteArray) ddc_msgbox_write = g_byte_array_new();
	const guint8 ddc_wfmt[] = {FU_DDC_I2C_ADDR_HOST_DEVICE, st_req->len | DDC_DATA_LEN_DFT};

	/* write = addr_src, sizeof(cmd + op + data), cmd, op, data, checksum */
	g_byte_array_append(ddc_msgbox_write, ddc_wfmt, sizeof(ddc_wfmt));
	g_byte_array_append(ddc_msgbox_write, st_req->data, st_req->len);

	chksum ^= FU_DDC_I2C_ADDR_DISPLAY_DEVICE;
	for (gsize i = 0; i < ddc_msgbox_write->len; i++)
		chksum ^= ddc_msgbox_write->data[i];
	g_byte_array_append(ddc_msgbox_write, &chksum, 1);

	/* print the raw data */
	fu_dump_raw(G_LOG_DOMAIN,
		    "DDC/CI write message",
		    ddc_msgbox_write->data,
		    ddc_msgbox_write->len);

	return fu_udev_device_pwrite(FU_UDEV_DEVICE(self->i2c_dev),
				     0x0,
				     ddc_msgbox_write->data,
				     ddc_msgbox_write->len,
				     error);
}

static GByteArray *
fu_mediatek_scaler_device_ddc_read(FuMediatekScalerDevice *self, GByteArray *st_req, GError **error)
{
	guint8 buf[0x40] = {0x00}; /* default 64 bytes */
	gsize report_data_sz = 0;
	guint8 checksum = 0;
	guint8 checksum_hw = 0;
	g_autoptr(GByteArray) st_res = g_byte_array_new();

	/* write for read */
	if (!fu_mediatek_scaler_device_ddc_write(self, st_req, error))
		return NULL;

	/* DDCCI spec requires host to wait at least 50 - 200ms before next message */
	fu_device_sleep(FU_DEVICE(self), FU_MEDIATEK_SCALER_DDC_MSG_DELAY_MS);

	/* read into tmp buffer */
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self->i2c_dev), 0x0, buf, sizeof(buf), error))
		return NULL;

	/* read buffer = addr(src) + length + data + checksum */
	fu_dump_raw(G_LOG_DOMAIN, "DDC/CI read buffer", buf, sizeof(buf));

	/* verify read buffer: [0] == source address */
	if (buf[0] != FU_DDC_I2C_ADDR_DISPLAY_DEVICE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid read buffer: addr(src) expected 0x%02x, got 0x%02x.",
			    (guint)FU_DDC_I2C_ADDR_DISPLAY_DEVICE,
			    buf[0]);
		return NULL;
	}

	/* verify read buffer: [1] as the length of data */
	if (buf[1] <= DDC_DATA_LEN_DFT) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid read buffer: size 0x%02x must greater than 0x%02x.",
			    buf[1],
			    (guint)DDC_DATA_LEN_DFT);
		return NULL;
	}

	/* verify read buffer: overflow guard from the length of data */
	report_data_sz = buf[1] - DDC_DATA_LEN_DFT;
	if (report_data_sz + 3 > sizeof(buf)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid read buffer: size 0x%02x exceeded 0x%02x",
			    (guint)report_data_sz,
			    (guint)sizeof(buf));
		return NULL;
	}

	/* verify read buffer: match the checksum */
	checksum ^= FU_DDC_I2C_ADDR_CHECKSUM;
	for (gsize i = 0; i < report_data_sz + 2; i++)
		checksum ^= buf[i];
	if (!fu_memread_uint8_safe(buf, sizeof(buf), report_data_sz + 2, &checksum_hw, error))
		return NULL;
	if (checksum_hw != checksum) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid read buffer, checksum expected 0x%02x, got 0x%02x.",
			    checksum,
			    checksum_hw);
		return NULL;
	}

	/* truncate the last byte which is the checksum value */
	g_byte_array_append(st_res, buf, report_data_sz + 2);

	/* print the raw data */
	fu_dump_raw(G_LOG_DOMAIN, "DDC/CI read report", st_res->data, st_res->len);
	return g_steal_pointer(&st_res);
}

static gboolean
fu_mediatek_scaler_device_set_ddc_priority(FuMediatekScalerDevice *self,
					   FuDdcciPriority priority,
					   GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GError) error_local = NULL;

	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_PRIORITY);
	fu_byte_array_append_uint8(st_req, priority);
	if (!fu_mediatek_scaler_device_ddc_write(self, st_req, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set priority %s [0x%x], unsupported display: %s",
			    fu_ddcci_priority_to_string(priority),
			    priority,
			    error_local->message);
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), FU_MEDIATEK_SCALER_DDC_MSG_DELAY_MS);
	return TRUE;
}

static gboolean
fu_mediatek_scaler_display_is_connected(FuMediatekScalerDevice *self, GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GByteArray) st_res = NULL;
	g_autoptr(GError) error_local = NULL;
	guint8 randval_req = 0;
	guint8 randval1 = g_random_int_range(1, 255);
	guint8 randval2 = g_random_int_range(1, 255);

	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_SUM);
	fu_byte_array_append_uint8(st_req, randval1);
	fu_byte_array_append_uint8(st_req, randval2);
	st_res = fu_mediatek_scaler_device_ddc_read(self, st_req, &error_local);
	if (st_res == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to read report: %s",
			    error_local->message);
		return FALSE;
	}
	if (!fu_memread_uint8_safe(st_res->data, st_res->len, 3, &randval_req, error))
		return FALSE;

	/* device unique feature */
	if (randval_req != (guint8)(randval1 + randval2)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsuccessful display feature test, expected 0x%02x, got 0x%02x.",
			    (guint8)(randval1 + randval2),
			    randval_req);
		return FALSE;
	}

	g_info("found mediatek display controller: %s, i2c-dev: %s",
	       fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)),
	       fu_udev_device_get_device_file(FU_UDEV_DEVICE(self->i2c_dev)));
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_display_is_connected_cb(FuDevice *device,
						  gpointer user_data,
						  GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	return fu_mediatek_scaler_display_is_connected(self, error);
}

static gchar *
fu_mediatek_scaler_device_get_hardware_version(FuDevice *device, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GByteArray) st_res = NULL;
	guint8 verbuf[4] = {0};

	/* get the hardware version */
	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_VERSION);
	fu_byte_array_append_uint8(st_req, 0x00);
	st_res = fu_mediatek_scaler_device_ddc_read(self, st_req, error);
	if (st_res == NULL)
		return NULL;
	if (!fu_memread_uint8_safe(st_res->data, st_res->len, 3, &verbuf[0], error))
		return NULL;
	if (!fu_memread_uint8_safe(st_res->data, st_res->len, 2, &verbuf[1], error))
		return NULL;
	if (!fu_memread_uint8_safe(st_res->data, st_res->len, 5, &verbuf[2], error))
		return NULL;
	if (!fu_memread_uint8_safe(st_res->data, st_res->len, 4, &verbuf[3], error))
		return NULL;
	return g_strdup_printf("%x.%x.%x.%x", verbuf[0], verbuf[1], verbuf[2], verbuf[3]);
}

static gboolean
fu_mediatek_scaler_device_ensure_firmware_version(FuMediatekScalerDevice *self, GError **error)
{
	guint32 version_raw = 0x0;
	g_autoptr(GByteArray) st_res = NULL;
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();

	/* get the installed firmware version */
	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_VERSION);
	fu_byte_array_append_uint8(st_req, 0x01);
	st_res = fu_mediatek_scaler_device_ddc_read(self, st_req, error);
	if (st_res == NULL)
		return FALSE;
	if (!fu_memread_uint32_safe(st_res->data,
				    st_res->len,
				    2,
				    &version_raw,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_open(FuDevice *device, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);

	/* proxy */
	if (!fu_device_open(FU_DEVICE(self->i2c_dev), error))
		return FALSE;

	/* set the target address -- should be safe */
	if (!fu_mediatek_scaler_ensure_device_address(self,
						      FU_DDC_I2C_ADDR_DISPLAY_DEVICE >> 1,
						      error))
		return FALSE;

	/* we know this is a Mediatek scaler now */
	if (fu_device_get_version_raw(device) != 0x0) {
		if (!fu_mediatek_scaler_device_set_ddc_priority(self, FU_DDCCI_PRIORITY_UP, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_close(FuDevice *device, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);

	/* do nothing for unsupported devices */
	if (self->i2c_dev == NULL)
		return TRUE;

	/* set the target address */
	if (!fu_mediatek_scaler_ensure_device_address(self,
						      FU_DDC_I2C_ADDR_DISPLAY_DEVICE >> 1,
						      error))
		return FALSE;

	/* reset DDC priority */
	if (!fu_mediatek_scaler_device_set_ddc_priority(self, FU_DDCCI_PRIORITY_NORMAL, error))
		return FALSE;

	/* proxy */
	if (!fu_device_close(FU_DEVICE(self->i2c_dev), error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_verify_controller_type(FuMediatekScalerDevice *self, GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GByteArray) st_res = NULL;
	g_autoptr(GError) error_local = NULL;
	guint32 controller_type = 0;

	fu_struct_ddc_cmd_set_opcode(st_req, FU_DDC_OPCODE_GET_VCP);
	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_CONTROLLER_TYPE);
	st_res = fu_mediatek_scaler_device_ddc_read(self, st_req, error);
	if (st_res == NULL)
		return FALSE;
	if (!fu_memread_uint32_safe(st_res->data,
				    st_res->len,
				    st_res->len - 4,
				    &controller_type,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;

	/* restrict to specific controller type */
	if (controller_type != FU_MEDIATEK_SCALER_SUPPORTED_CONTROLLER_TYPE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "0x%x is not supported",
			    controller_type);
		return FALSE;
	}

	/* success */
	fu_device_sleep(FU_DEVICE(self), FU_MEDIATEK_SCALER_DDC_MSG_DELAY_MS);
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_setup(FuDevice *device, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	g_autofree gchar *hw_ver = NULL;

	/* verify the controller type */
	if (!fu_mediatek_scaler_device_verify_controller_type(self, error)) {
		g_prefix_error(error, "invalid controller type: ");
		return FALSE;
	}

	/* mediatek display is connected */
	if (!fu_mediatek_scaler_display_is_connected(self, error))
		return FALSE;

	/* prioritize DDC/CI -- FuDevice->open() did not do this as the version is not set */
	if (!fu_mediatek_scaler_device_set_ddc_priority(self, FU_DDCCI_PRIORITY_UP, error))
		return FALSE;

	/* set hardware version */
	hw_ver = fu_mediatek_scaler_device_get_hardware_version(device, error);
	if (hw_ver == NULL)
		return FALSE;
	fu_device_add_instance_str(device, "HWVER", hw_ver);
	if (!fu_device_build_instance_id(device, error, "DISPLAY", "VID", "PID", "HWVER", NULL))
		return FALSE;

	/* get details */
	if (!fu_mediatek_scaler_device_ensure_firmware_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_probe(FuDevice *device, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	g_autofree gchar *vendor_id = NULL;
	g_autoptr(FuUdevDevice) udev_parent = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_mediatek_scaler_device_parent_class)->probe(device, error))
		return FALSE;

	/* set vid and pid from PCI bus */
	udev_parent = fu_udev_device_get_parent_with_subsystem(FU_UDEV_DEVICE(device), "pci");
	if (udev_parent == NULL)
		return FALSE;
	if (!fu_device_probe(FU_DEVICE(udev_parent), error))
		return FALSE;

	fu_device_add_instance_u16(device, "VID", fu_udev_device_get_subsystem_vendor(udev_parent));
	fu_device_add_instance_u16(device, "PID", fu_udev_device_get_subsystem_model(udev_parent));
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "PCI",
					      "VID",
					      "PID",
					      NULL))
		return FALSE;
	if (!fu_device_has_private_flag(device, FU_MEDIATEK_SCALER_DEVICE_FLAG_PROBE_VCP)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "%04X:%04X: is not supported",
			    fu_udev_device_get_subsystem_vendor(udev_parent),
			    fu_udev_device_get_subsystem_model(udev_parent));
		return FALSE;
	}

	/* determine the i2c_dev for dp aux dev */
	if (!fu_mediatek_scaler_device_use_aux_dev(self, error))
		return FALSE;

	/* add IDs */
	vendor_id = g_strdup_printf("PCI:0x%04X", fu_udev_device_get_subsystem_vendor(udev_parent));
	fu_device_add_vendor_id(device, vendor_id);
	fu_device_set_physical_id(device, fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)));

	/* success */
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_set_recv_info(FuDevice *device, gsize fw_sz, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_UPDATE_PREP);
	fu_byte_array_append_uint32(st_req, fw_sz, G_LITTLE_ENDIAN);
	return fu_mediatek_scaler_device_ddc_write(self, st_req, error);
}

static gboolean
fu_mediatek_scaler_device_get_data_ack_size(FuDevice *device, guint32 *ack_sz, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GByteArray) st_res = NULL;

	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_UPDATE_ACK);
	st_res = fu_mediatek_scaler_device_ddc_read(self, st_req, error);
	if (st_res == NULL)
		return FALSE;
	if (!fu_memread_uint32_safe(st_res->data, st_res->len, 2, ack_sz, G_LITTLE_ENDIAN, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_prepare_update_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint32 acksz = 0;
	gsize fw_sz = *(gsize *)user_data;

	/* set the file length that to be transmit*/
	if (!fu_mediatek_scaler_device_set_recv_info(device, fw_sz, error))
		return FALSE;

	/* extra delay time needed */
	fu_device_sleep(device, 100);

	/* device accepted the file length for data transition */
	if (!fu_mediatek_scaler_device_get_data_ack_size(device, &acksz, error))
		return FALSE;
	if (fw_sz != (gsize)acksz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device nak the incoming filesize, requested: %" G_GSIZE_FORMAT
			    ", ack: %u",
			    fw_sz,
			    acksz);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_prepare_update(FuDevice *device, gsize fw_sz, GError **error)
{
	if (!fu_device_retry_full(device,
				  fu_mediatek_scaler_device_prepare_update_cb,
				  DDC_RW_MAX_RETRY_CNT,
				  10, /* ms */
				  &fw_sz,
				  error)) {
		g_prefix_error(error, "failed to prepare update: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_set_data(FuMediatekScalerDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(FuChunkArray) chk_slices = NULL;
	g_autoptr(GBytes) chk_bytes = fu_chunk_get_bytes(chk);

	/* smaller slices to accodomate pch variants */
	chk_slices = fu_chunk_array_new_from_bytes(chk_bytes, 0x00, DDC_DATA_FRAGEMENT_SIZE);
	for (guint i = 0; i < fu_chunk_array_length(chk_slices); i++) {
		g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
		g_autoptr(FuChunk) chk_slice = fu_chunk_array_index(chk_slices, i);

		fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_SET_DATA);
		g_byte_array_append(st_req,
				    fu_chunk_get_data(chk_slice),
				    (guint)fu_chunk_get_data_sz(chk_slice));
		if (!fu_mediatek_scaler_device_ddc_write(self, st_req, error)) {
			g_prefix_error(error, "failed to send firmware to device: ");
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), FU_MEDIATEK_SCALER_CHUNK_SENT_DELAY_MS);
	}
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_get_staged_data(FuMediatekScalerDevice *self,
					  guint16 *chksum,
					  guint32 *pktcnt,
					  GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GByteArray) st_res = NULL;

	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_GET_STAGED);
	st_res = fu_mediatek_scaler_device_ddc_read(self, st_req, error);
	if (st_res == NULL)
		return FALSE;
	if (!fu_memread_uint16_safe(st_res->data, st_res->len, 2, chksum, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_memread_uint32_safe(st_res->data, st_res->len, 4, pktcnt, G_LITTLE_ENDIAN, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_check_sent_info(FuMediatekScalerDevice *self,
					  FuChunk *chk,
					  guint32 sent_size,
					  GError **error)
{
	guint16 chksum = 0;
	guint32 pktcnt = 0;
	guint16 sum16 = 0;

	if (!fu_mediatek_scaler_device_get_staged_data(self, &chksum, &pktcnt, error)) {
		g_prefix_error(error, "failed to get the staged data: ");
		return FALSE;
	}

	/* verify the staged packets on chip */
	if (sent_size != pktcnt) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "data packet size mismatched, expected: %X, chip got: %X",
			    sent_size,
			    pktcnt);
		return FALSE;
	}

	/* verify the checksum on chip */
	sum16 = fu_sum16(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	if (sum16 != chksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "data packet checksum mismatched, expected: %X, chip got: %X",
			    sum16,
			    chksum);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_run_isp(FuMediatekScalerDevice *self, guint16 chksum, GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_COMMIT_FW);
	fu_byte_array_append_uint16(st_req, chksum, G_LITTLE_ENDIAN);
	return fu_mediatek_scaler_device_ddc_write(self, st_req, error);
}

static gboolean
fu_mediatek_scaler_device_commit_firmware(FuMediatekScalerDevice *self, GBytes *fw, GError **error)
{
	if (!(fu_mediatek_scaler_device_run_isp(self, fu_sum16_bytes(fw), error))) {
		g_prefix_error(error, "failed to commit firmware: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_set_isp_reboot(FuMediatekScalerDevice *self, GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GError) error_local = NULL;

	/* device will reboot after this, so the write will timed out fail */
	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_REBOOT);
	if (!fu_mediatek_scaler_device_ddc_write(self, st_req, &error_local)) {
		if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to set isp reboot: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_get_isp_status(FuMediatekScalerDevice *self,
					 guint8 *isp_status,
					 GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	g_autoptr(GByteArray) st_res = NULL;

	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_GET_ISP_MODE);

	st_res = fu_mediatek_scaler_device_ddc_read(self, st_req, error);
	if (st_res == NULL)
		return FALSE;

	if (!fu_memread_uint8_safe(st_res->data, st_res->len, 2, isp_status, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_is_update_success_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	guint8 isp_status = 0;

	if (!fu_mediatek_scaler_device_get_isp_status(self, &isp_status, error))
		return FALSE;

	if (isp_status != FU_MEDIATEK_SCALER_ISP_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "incorrect isp status, expected: 0x%x, got: 0x%x",
			    (guint)FU_MEDIATEK_SCALER_ISP_STATUS_SUCCESS,
			    isp_status);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_verify(FuDevice *device, GError **error)
{
	if (!fu_device_retry_full(device,
				  fu_mediatek_scaler_device_display_is_connected_cb,
				  FU_MEDIATEK_SCALER_DEVICE_PRESENT_RETRY,
				  FU_MEDIATEK_SCALER_DEVICE_POLL_INTERVAL,
				  NULL,
				  error)) {
		g_prefix_error(error,
			       "display controller did not reconnect after %u retries: ",
			       (guint)FU_MEDIATEK_SCALER_DEVICE_PRESENT_RETRY);
		return FALSE;
	}

	/* ensure isp status */
	if (!fu_device_retry_full(device,
				  fu_mediatek_scaler_device_is_update_success_cb,
				  FU_MEDIATEK_SCALER_DEVICE_PRESENT_RETRY,
				  FU_MEDIATEK_SCALER_DEVICE_POLL_INTERVAL,
				  NULL,
				  error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_chunk_data_is_blank(FuChunk *chk)
{
	const guint8 *data = fu_chunk_get_data(chk);
	for (gsize idx = 0; idx < fu_chunk_get_data_sz(chk); idx++)
		if (data[idx] != 0xFF)
			return FALSE;
	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_set_data_fast_forward(FuMediatekScalerDevice *self,
						guint32 sent_sz,
						GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_ddc_cmd_new();
	fu_struct_ddc_cmd_set_vcp_code(st_req, FU_DDC_VCP_CODE_SET_DATA_FF);
	fu_byte_array_append_uint32(st_req, sent_sz, G_LITTLE_ENDIAN);
	return fu_mediatek_scaler_device_ddc_write(self, st_req, error);
}

static gboolean
fu_mediatek_scaler_device_write_chunk(FuDevice *device, gpointer user_data, GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	FuMediatekScalerWriteChunkHelper *helper = (FuMediatekScalerWriteChunkHelper *)user_data;

	/* fast forward if possible */
	if (fu_mediatek_scaler_device_chunk_data_is_blank(helper->chk)) {
		/* fast forward if chunk is empty */
		if (!fu_mediatek_scaler_device_set_data_fast_forward(self, helper->sent_sz, error))
			return FALSE;
	} else {
		/* set data per fragment size */
		if (!fu_mediatek_scaler_device_set_data(self, helper->chk, error))
			return FALSE;
	}

	/* verify the sent data chunk */
	if (!fu_mediatek_scaler_device_check_sent_info(self, helper->chk, helper->sent_sz, error)) {
		/* restore the data size counter */
		if (!fu_mediatek_scaler_device_set_data_fast_forward(
			self,
			helper->sent_sz - fu_chunk_get_data_sz(helper->chk),
			error))
			return FALSE;
	}

	/* ff to reset the checksum */
	return fu_mediatek_scaler_device_set_data_fast_forward(self, helper->sent_sz, error);
}

static gboolean
fu_mediatek_scaler_device_write_firmware_impl(FuMediatekScalerDevice *self,
					      GBytes *fw,
					      FuProgress *progress,
					      GError **error)
{
	guint32 sent_sz = 0x0;
	g_autoptr(FuChunkArray) chunks =
	    fu_chunk_array_new_from_bytes(fw, 0x00, DDC_DATA_PAGE_SIZE);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		FuMediatekScalerWriteChunkHelper helper_wchunk = {0x0};
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i);
		if (chk == NULL)
			return FALSE;

		/* data size already sent to chip */
		sent_sz += fu_chunk_get_data_sz(chk);

		/* retry writing data chunk */
		helper_wchunk.chk = chk;
		helper_wchunk.sent_sz = sent_sz;
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_mediatek_scaler_device_write_chunk,
					  DDC_RW_MAX_RETRY_CNT,
					  FU_MEDIATEK_SCALER_DDC_MSG_DELAY_MS,
					  &helper_wchunk,
					  error)) {
			g_prefix_error(error, "writing chunk exceeded the maximum retries");
			return FALSE;
		}

		/* write chunk successfully, update the progress */
		fu_progress_step_done(progress);

		g_debug("data size sent to chip: 0x%x", sent_sz);
	}

	return TRUE;
}

static gboolean
fu_mediatek_scaler_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuMediatekScalerDevice *self = FU_MEDIATEK_SCALER_DEVICE(device);
	gsize fw_size = 0;
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 76, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "commit");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 12, "verify");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "reset");

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* prepare the device to accept firmware image */
	fw_size = g_bytes_get_size(fw);
	if (!fu_mediatek_scaler_device_prepare_update(device, fw_size, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write firmware to device */
	if (!fu_mediatek_scaler_device_write_firmware_impl(self, fw, progress, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send ISP command to commit the update */
	if (!fu_mediatek_scaler_device_commit_firmware(self, fw, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify display and ISP status; for bank 1 devices 0xF8 will do self-reboot */
	if (!fu_mediatek_scaler_device_verify(device, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* for bank 2 update */
	if (fu_device_has_private_flag(device, FWUPD_MEDIATEK_SCALER_FLAG_BANK2_ONLY)) {
		/* send reboot command to take effect immediately */
		if (!(fu_mediatek_scaler_device_set_isp_reboot(self, error)))
			return FALSE;

		/* ensure device is back */
		if (!fu_device_retry_full(device,
					  fu_mediatek_scaler_device_display_is_connected_cb,
					  FU_MEDIATEK_SCALER_DEVICE_PRESENT_RETRY,
					  FU_MEDIATEK_SCALER_DEVICE_POLL_INTERVAL,
					  NULL,
					  error)) {
			g_prefix_error(error,
				       "display controller did not reconnect after %u retries: ",
				       (guint)FU_MEDIATEK_SCALER_DEVICE_PRESENT_RETRY);
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_mediatek_scaler_device_prepare_firmware(FuDevice *device,
					   GBytes *fw,
					   FwupdInstallFlags flags,
					   GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_mediatek_scaler_firmware_new();

	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	g_info("firmware version old: %s, new: %s",
	       fu_device_get_version(device),
	       fu_firmware_get_version(firmware));
	return g_steal_pointer(&firmware);
}

static gchar *
fu_mediatek_scaler_device_convert_version(FuDevice *self, guint64 version_raw)
{
	return mediatek_scaler_device_version_to_string(version_raw);
}

static void
fu_mediatek_scaler_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_mediatek_scaler_device_init(FuMediatekScalerDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_vendor(FU_DEVICE(self), "Mediatek");
	fu_device_add_protocol(FU_DEVICE(self), "com.mediatek.scaler");
	fu_device_set_name(FU_DEVICE(self), "Display Controller");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_set_firmware_size_max(FU_DEVICE(self), FU_MEDIATEK_SCALER_FW_SIZE_MAX);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_MEDIATEK_SCALER_DEVICE_FLAG_PROBE_VCP,
					"probe-vcp");
	fu_device_register_private_flag(FU_DEVICE(self),
					FWUPD_MEDIATEK_SCALER_FLAG_BANK2_ONLY,
					"bank2-only");
}

static void
fu_mediatek_scaler_device_class_init(FuMediatekScalerDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->convert_version = fu_mediatek_scaler_device_convert_version;
	device_class->probe = fu_mediatek_scaler_device_probe;
	device_class->setup = fu_mediatek_scaler_device_setup;
	device_class->open = fu_mediatek_scaler_device_open;
	device_class->close = fu_mediatek_scaler_device_close;
	device_class->prepare_firmware = fu_mediatek_scaler_device_prepare_firmware;
	device_class->write_firmware = fu_mediatek_scaler_device_write_firmware;
	device_class->reload = fu_mediatek_scaler_device_setup;
	device_class->set_progress = fu_mediatek_scaler_device_set_progress;
	device_class->to_string = fu_mediatek_scaler_device_to_string;
}
