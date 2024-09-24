/*
 * Copyright 2022 Aaron Skomra <aaron.skomra@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-device.h"
#include "fu-wac-module-scaler.h"
#include "fu-wac-struct.h"

struct _FuWacModuleScaler {
	FuWacModule parent_instance;
};

G_DEFINE_TYPE(FuWacModuleScaler, fu_wac_module_scaler, FU_TYPE_WAC_MODULE)

#define FU_WAC_MODULE_SCALER_CRC8_POLYNOMIAL 0x07
#define FU_WAC_MODULE_SCALER_PAYLOAD_SZ	     256
#define FU_WAC_MODULE_SCALER_START_NORMAL    0x00
#define FU_WAC_MODULE_SCALER_START_FULLERASE 0xFE

typedef struct __attribute__((__packed__)) { /* nocheck:blocked */
	guint8 addr[3];
	guint8 crc;
	guint8 cdata[FU_WAC_MODULE_SCALER_PAYLOAD_SZ];
} FuWacModuleScalerBlockData;

static GPtrArray *
fu_wac_module_scaler_parse_blocks(const guint8 *data, gsize sz, GError **error)
{
	GPtrArray *blocks = g_ptr_array_new_with_free_func(g_free);
	for (guint addr = 0x0; addr < sz; addr += FU_WAC_MODULE_SCALER_PAYLOAD_SZ) {
		g_autofree FuWacModuleScalerBlockData *bd = NULL;
		gsize cdata_sz = FU_WAC_MODULE_SCALER_PAYLOAD_SZ;

		bd = g_new0(FuWacModuleScalerBlockData, 1);

		fu_memwrite_uint24(bd->addr, addr, G_BIG_ENDIAN);

		memset(bd->cdata, 0xff, sizeof(bd->cdata));

		if (addr + FU_WAC_MODULE_SCALER_PAYLOAD_SZ >= sz)
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
		bd->crc = fu_crc8(FU_CRC_KIND_B8_STANDARD, bd->cdata, sizeof(bd->cdata));
		g_ptr_array_add(blocks, g_steal_pointer(&bd));
	}
	return blocks;
}

static gboolean
fu_wac_module_scaler_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	const guint8 *data;
	gsize len = 0;
	const guint8 buf_start[] = {FU_WAC_MODULE_SCALER_START_NORMAL};
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
	if (fw == NULL) {
		g_prefix_error(error, "wacom scaler module failed to get bytes: ");
		return FALSE;
	}

	/* build each data packet */
	data = g_bytes_get_data(fw, &len);
	blocks = fu_wac_module_scaler_parse_blocks(data, len, error);
	if (blocks == NULL) {
		g_prefix_error(error, "wacom scaler module failed to parse blocks: ");
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
		g_prefix_error(error, "wacom scaler module failed to erase: ");
		return FALSE;
	}

	fu_progress_step_done(progress);

	/* data */
	for (guint i = 0; i < blocks->len; i++) {
		FuWacModuleScalerBlockData *bd = g_ptr_array_index(blocks, i);
		guint8 buf[FU_WAC_MODULE_SCALER_PAYLOAD_SZ + 4];
		g_autoptr(GBytes) blob_chunk = NULL;

		/* build data packet */
		memset(buf, 0xff, sizeof(buf));
		memcpy(&buf[0], bd->addr, 3); /* nocheck:blocked */
		buf[3] = bd->crc;
		memcpy(&buf[4], bd->cdata, sizeof(bd->cdata)); /* nocheck:blocked */
		blob_chunk = g_bytes_new(buf, sizeof(*bd));

		if (!fu_wac_module_set_feature(self,
					       FU_WAC_MODULE_COMMAND_DATA,
					       blob_chunk,
					       fu_progress_get_child(progress),
					       FU_WAC_MODULE_POLL_INTERVAL,
					       FU_WAC_MODULE_DATA_TIMEOUT,
					       error)) {
			g_prefix_error(error, "wacom scaler module failed to write: ");
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
		g_prefix_error(error, "wacom scaler module failed to end: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_wac_module_scaler_init(FuWacModuleScaler *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration(FU_DEVICE(self), 120);
}

static void
fu_wac_module_scaler_class_init(FuWacModuleScalerClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_wac_module_scaler_write_firmware;
}

FuWacModule *
fu_wac_module_scaler_new(FuDevice *proxy)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_SCALER,
			      "proxy",
			      proxy,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_SCALER,
			      NULL);
	return module;
}
