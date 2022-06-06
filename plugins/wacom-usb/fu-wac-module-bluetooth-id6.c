/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jason Gerecke <killertofu@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-device.h"
#include "fu-wac-module-bluetooth-id6.h"

struct _FuWacModuleBluetoothId6 {
	FuWacModule parent_instance;
};

G_DEFINE_TYPE(FuWacModuleBluetoothId6, fu_wac_module_bluetooth_id6, FU_TYPE_WAC_MODULE)

#define FU_WAC_MODULE_BLUETOOTH_ID6_CRC8_POLYNOMIAL 0x31
#define FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ	    256
#define FU_WAC_MODULE_BLUETOOTH_ID6_START_NORMAL    0x00
#define FU_WAC_MODULE_BLUETOOTH_ID6_START_FULLERASE 0xFE

typedef struct {
	guint8 preamble[2];
	guint8 crc;
	guint8 addr[4];
	guint8 cdata[FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ];
} FuWacModuleBluetoothId6BlockData;

static guint8
fu_wac_module_bluetooth_id6_reverse_bits(guint8 value)
{
	guint8 reverse = 0;

	for (gint i = 0; i < 8; i++) {
		reverse <<= 1;
		reverse |= (value & 0x01);
		value >>= 1;
	}
	return reverse;
}

static guint8
fu_wac_module_bluetooth_id6_calculate_crc(const guint8 *data, gsize sz)
{
	guint8 crc = ~fu_crc8_full(data, sz, 0x00, FU_WAC_MODULE_BLUETOOTH_ID6_CRC8_POLYNOMIAL);
	return fu_wac_module_bluetooth_id6_reverse_bits(crc);
}

static GPtrArray *
fu_wac_module_bluetooth_id6_parse_blocks(const guint8 *data, gsize sz, GError **error)
{
	const guint8 preamble[] = {0x00, 0x01};
	GPtrArray *blocks = g_ptr_array_new_with_free_func(g_free);
	for (guint addr = 0x0; addr < sz; addr += FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ) {
		g_autofree FuWacModuleBluetoothId6BlockData *bd = NULL;
		gsize cdata_sz = FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ;

		bd = g_new0(FuWacModuleBluetoothId6BlockData, 1);
		memcpy(bd->preamble, preamble, sizeof(preamble));
		bd->addr[0] = 0;
		bd->addr[1] = 0;
		bd->addr[2] = 0;
		bd->addr[3] = 0;
		memset(bd->cdata, 0xff, FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ);

		/* if file is not in multiples of payload size */
		if (addr + FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ >= sz)
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
		bd->crc = fu_wac_module_bluetooth_id6_calculate_crc(
		    bd->cdata,
		    FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ);
		g_ptr_array_add(blocks, g_steal_pointer(&bd));
	}
	return blocks;
}

static gboolean
fu_wac_module_bluetooth_id6_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	const guint8 *data;
	gsize len = 0;
	const guint8 buf_start[] = {FU_WAC_MODULE_BLUETOOTH_ID6_START_NORMAL};
	g_autoptr(GPtrArray) blocks = NULL;
	g_autoptr(GBytes) blob_start = g_bytes_new_static(buf_start, 1);
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 8, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 59, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 33, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* build each data packet */
	data = g_bytes_get_data(fw, &len);
	blocks = fu_wac_module_bluetooth_id6_parse_blocks(data, len, error);
	if (blocks == NULL)
		return FALSE;

	/* start, which will erase the module */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_START,
				       blob_start,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_ERASE_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* data */
	for (guint i = 0; i < blocks->len; i++) {
		FuWacModuleBluetoothId6BlockData *bd = g_ptr_array_index(blocks, i);
		guint8 buf[FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ + 7];
		g_autoptr(GBytes) blob_chunk = NULL;

		/* build data packet */
		memset(buf, 0xff, sizeof(buf));
		memcpy(&buf[0], bd->preamble, 2);
		buf[2] = bd->crc;
		memcpy(&buf[3], bd->addr, 4);
		memcpy(&buf[7], bd->cdata, sizeof(bd->cdata));
		blob_chunk = g_bytes_new(buf, sizeof(buf));
		if (!fu_wac_module_set_feature(self,
					       FU_WAC_MODULE_COMMAND_DATA,
					       blob_chunk,
					       fu_progress_get_child(progress),
					       FU_WAC_MODULE_WRITE_TIMEOUT,
					       error))
			return FALSE;

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
				       FU_WAC_MODULE_COMMIT_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_wac_module_bluetooth_id6_init(FuWacModuleBluetoothId6 *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration(FU_DEVICE(self), 120);
}

static void
fu_wac_module_bluetooth_id6_class_init(FuWacModuleBluetoothId6Class *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_wac_module_bluetooth_id6_write_firmware;
}

FuWacModule *
fu_wac_module_bluetooth_id6_new(FuContext *context, GUsbDevice *usb_device)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_BLUETOOTH_ID6,
			      "context",
			      context,
			      "usb-device",
			      usb_device,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_BLUETOOTH_ID6,
			      NULL);
	return module;
}
