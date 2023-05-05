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
#include "fu-wac-struct.h"

struct _FuWacModuleBluetoothId6 {
	FuWacModule parent_instance;
};

G_DEFINE_TYPE(FuWacModuleBluetoothId6, fu_wac_module_bluetooth_id6, FU_TYPE_WAC_MODULE)

#define FU_WAC_MODULE_BLUETOOTH_ID6_CRC8_POLYNOMIAL 0x31
#define FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ	    256
#define FU_WAC_MODULE_BLUETOOTH_ID6_START_NORMAL    0x00
#define FU_WAC_MODULE_BLUETOOTH_ID6_START_FULLERASE 0xFE

#define FU_WAC_MODULE_BLUETOOTH_ID6_WRITE_TIMEOUT 8000 /* ms */

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

static gboolean
fu_wac_module_bluetooth_id6_write_blob(FuWacModule *self,
				       GBytes *fw,
				       FuProgress *progress,
				       GError **error)
{
	g_autoptr(GPtrArray) chunks =
	    fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf[FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ + 7] = {0x00, 0x01, 0xFF};
		g_autoptr(GBytes) blob_chunk = NULL;

		/* build data packet */
		fu_memwrite_uint32(buf + 0x3, 0x0, G_LITTLE_ENDIAN); /* addr, always zero */
		memcpy(buf + 0x7, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		buf[2] = fu_wac_module_bluetooth_id6_calculate_crc(
		    buf + 0x7,
		    FU_WAC_MODULE_BLUETOOTH_ID6_PAYLOAD_SZ); /* include 0xFF for the possibly
								incomplete last chunk */
		blob_chunk = g_bytes_new(buf, sizeof(buf));
		g_debug("writing block %u of %u", i, chunks->len - 1);
		if (!fu_wac_module_set_feature(self,
					       FU_WAC_MODULE_COMMAND_DATA,
					       blob_chunk,
					       fu_progress_get_child(progress),
					       FU_WAC_MODULE_BLUETOOTH_ID6_WRITE_TIMEOUT,
					       error)) {
			g_prefix_error(error, "failed to write block %u of %u: ", i, chunks->len);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wac_module_bluetooth_id6_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	const guint8 buf_start[] = {FU_WAC_MODULE_BLUETOOTH_ID6_START_NORMAL};
	g_autoptr(GBytes) blob_start = g_bytes_new_static(buf_start, sizeof(buf_start));
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 8, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 59, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 33, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL) {
		g_prefix_error(error, "wacom bluetooth-id6 module failed to get bytes: ");
		return FALSE;
	}

	/* start, which will erase the module */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_START,
				       blob_start,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_ERASE_TIMEOUT,
				       error)) {
		g_prefix_error(error, "wacom bluetooth-id6 module failed to erase: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* data */
	if (!fu_wac_module_bluetooth_id6_write_blob(self,
						    fw,
						    fu_progress_get_child(progress),
						    error)) {
		g_prefix_error(error, "wacom bluetooth-id6 module failed to write: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* end */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_END,
				       NULL,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_COMMIT_TIMEOUT,
				       error)) {
		g_prefix_error(error, "wacom bluetooth-id6 module failed to end: ");
		return FALSE;
	}
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
fu_wac_module_bluetooth_id6_new(FuDevice *proxy)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_BLUETOOTH_ID6,
			      "proxy",
			      proxy,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_BLUETOOTH_ID6,
			      NULL);
	return module;
}
