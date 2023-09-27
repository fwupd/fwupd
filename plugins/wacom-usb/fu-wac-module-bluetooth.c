/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-device.h"
#include "fu-wac-module-bluetooth.h"
#include "fu-wac-struct.h"

struct _FuWacModuleBluetooth {
	FuWacModule parent_instance;
};

G_DEFINE_TYPE(FuWacModuleBluetooth, fu_wac_module_bluetooth, FU_TYPE_WAC_MODULE)

#define FU_WAC_MODULE_BLUETOOTH_PAYLOAD_SZ	    256
#define FU_WAC_MODULE_BLUETOOTH_ADDR_USERDATA_START 0x3000
#define FU_WAC_MODULE_BLUETOOTH_ADDR_USERDATA_STOP  0x8000

typedef struct {
	guint8 preamble[7];
	guint32 addr;
	guint8 crc;
	guint8 cdata[FU_WAC_MODULE_BLUETOOTH_PAYLOAD_SZ];
} FuWacModuleBluetoothBlockData;

static void
fu_wac_module_bluetooth_calculate_crc_byte(guint8 *crc, guint8 data)
{
	guint8 c[8];
	guint8 m[8];
	guint8 r[8];

	/* find out what bits are set */
	for (guint i = 0; i < 8; i++) {
		c[i] = (*crc & (1 << i)) != 0;
		m[i] = (data & (1 << i)) != 0;
	}

	/* do CRC on byte */
	r[7] = (c[7] ^ m[4] ^ c[3] ^ m[3] ^ c[4] ^ m[6] ^ c[1] ^ m[0]);
	r[6] = (c[6] ^ m[5] ^ c[2] ^ m[4] ^ c[3] ^ m[7] ^ c[0] ^ m[1]);
	r[5] = (c[5] ^ m[6] ^ c[1] ^ m[5] ^ c[2] ^ m[2]);
	r[4] = (c[4] ^ m[7] ^ c[0] ^ m[6] ^ c[1] ^ m[3]);
	r[3] = (m[7] ^ m[0] ^ c[7] ^ c[0] ^ m[3] ^ c[4] ^ m[6] ^ c[1]);
	r[2] = (m[1] ^ c[6] ^ m[0] ^ c[7] ^ m[3] ^ c[4] ^ m[7] ^ c[0] ^ m[6] ^ c[1]);
	r[1] = (m[2] ^ c[5] ^ m[1] ^ c[6] ^ m[4] ^ c[3] ^ m[7] ^ c[0]);
	r[0] = (m[3] ^ c[4] ^ m[2] ^ c[5] ^ m[5] ^ c[2]);

	/* copy back into CRC */
	*crc = 0;
	for (guint i = 0; i < 8; i++) {
		if (r[i] == 0)
			continue;
		*crc |= (1 << i);
	}
}

static guint8
fu_wac_module_bluetooth_calculate_crc(const guint8 *data, gsize sz)
{
	guint8 crc = 0;
	for (gsize i = 0; i < sz; i++)
		fu_wac_module_bluetooth_calculate_crc_byte(&crc, data[i]);
	return crc;
}

static GPtrArray *
fu_wac_module_bluetooth_parse_blocks(const guint8 *data,
				     gsize sz,
				     gboolean skip_user_data,
				     GError **error)
{
	const guint8 preamble[] = {0x02, 0x00, 0x0f, 0x06, 0x01, 0x08, 0x01};
	GPtrArray *blocks = g_ptr_array_new_with_free_func(g_free);
	for (guint addr = 0x0; addr < sz; addr += FU_WAC_MODULE_BLUETOOTH_PAYLOAD_SZ) {
		g_autofree FuWacModuleBluetoothBlockData *bd = NULL;
		gsize cdata_sz = FU_WAC_MODULE_BLUETOOTH_PAYLOAD_SZ;

		/* user data area */
		if (skip_user_data && addr >= FU_WAC_MODULE_BLUETOOTH_ADDR_USERDATA_START &&
		    addr < FU_WAC_MODULE_BLUETOOTH_ADDR_USERDATA_STOP)
			continue;

		bd = g_new0(FuWacModuleBluetoothBlockData, 1);
		bd->addr = addr;
		memcpy(bd->preamble, preamble, sizeof(preamble));
		memset(bd->cdata, 0xff, FU_WAC_MODULE_BLUETOOTH_PAYLOAD_SZ);

		/* if file is not in multiples of payload size */
		if (addr + FU_WAC_MODULE_BLUETOOTH_PAYLOAD_SZ >= sz)
			cdata_sz = sz - addr;
		if (!fu_memcpy_safe(bd->cdata,
				    sizeof(bd->cdata),
				    0x0, /* dst */
				    data,
				    sz,
				    addr, /* src */
				    cdata_sz,
				    error))
			return NULL;
		bd->crc = fu_wac_module_bluetooth_calculate_crc(bd->cdata,
								FU_WAC_MODULE_BLUETOOTH_PAYLOAD_SZ);
		g_ptr_array_add(blocks, g_steal_pointer(&bd));
	}
	return blocks;
}

static gboolean
fu_wac_module_bluetooth_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	const guint8 *data;
	gsize len = 0;
	const guint8 buf_start[] = {0x00};
	g_autoptr(GPtrArray) blocks = NULL;
	g_autoptr(GBytes) blob_start = g_bytes_new_static(buf_start, 1);
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 79, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL) {
		g_prefix_error(error, "wacom bluetooth module failed to get bytes: ");
		return FALSE;
	}

	/* build each data packet */
	data = g_bytes_get_data(fw, &len);
	blocks = fu_wac_module_bluetooth_parse_blocks(data, len, TRUE, error);
	if (blocks == NULL) {
		g_prefix_error(error, "wacom bluetooth module failed to parse: ");
		return FALSE;
	}

	/* start, which will erase the module */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_START,
				       blob_start,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_POLL_INTERVAL,
				       FU_WAC_MODULE_START_TIMEOUT,
				       error)) {
		g_prefix_error(error, "wacom bluetooth module failed to erase: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* data */
	for (guint i = 0; i < blocks->len; i++) {
		FuWacModuleBluetoothBlockData *bd = g_ptr_array_index(blocks, i);
		guint8 buf[256 + 11];
		g_autoptr(GBytes) blob_chunk = NULL;

		/* build data packet */
		memset(buf, 0xff, sizeof(buf));
		memcpy(&buf[0], bd->preamble, 7);
		fu_memwrite_uint24(buf + 0x7, bd->addr, G_LITTLE_ENDIAN);
		buf[10] = bd->crc;
		memcpy(&buf[11], bd->cdata, sizeof(bd->cdata));
		blob_chunk = g_bytes_new(buf, sizeof(buf));
		if (!fu_wac_module_set_feature(self,
					       FU_WAC_MODULE_COMMAND_DATA,
					       blob_chunk,
					       fu_progress_get_child(progress),
					       FU_WAC_MODULE_POLL_INTERVAL,
					       FU_WAC_MODULE_DATA_TIMEOUT,
					       error)) {
			g_prefix_error(error, "wacom bluetooth module failed to write: ");
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						i + 1,
						blocks->len);
	}
	fu_progress_step_done(progress);

	/* end */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_END,
				       NULL,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_POLL_INTERVAL,
				       FU_WAC_MODULE_END_TIMEOUT,
				       error)) {
		g_prefix_error(error, "wacom bluetooth module failed to end: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_wac_module_bluetooth_init(FuWacModuleBluetooth *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration(FU_DEVICE(self), 30);
}

static void
fu_wac_module_bluetooth_class_init(FuWacModuleBluetoothClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_wac_module_bluetooth_write_firmware;
}

FuWacModule *
fu_wac_module_bluetooth_new(FuDevice *proxy)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_BLUETOOTH,
			      "proxy",
			      proxy,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_BLUETOOTH,
			      NULL);
	return module;
}
