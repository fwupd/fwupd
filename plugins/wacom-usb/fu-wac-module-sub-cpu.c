/*
 * Copyright 2024 Jason Gerecke <jason.gerecke@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-device.h"
#include "fu-wac-module-sub-cpu.h"
#include "fu-wac-struct.h"

struct _FuWacModuleSubCpu {
	FuWacModule parent_instance;
};

G_DEFINE_TYPE(FuWacModuleSubCpu, fu_wac_module_sub_cpu, FU_TYPE_WAC_MODULE)

#define FU_WAC_MODULE_SUB_CPU_PAYLOAD_SZ   256
#define FU_WAC_MODULE_SUB_CPU_START_NORMAL 0x00

static FuChunk *
fu_wac_module_sub_cpu_create_chunk(GPtrArray *srec_records, guint32 *record_num, GError **error)
{
	FuChunk *chunk;
	guint32 base_addr = 0;
	guint32 expect_addr = 0;
	g_autoptr(GByteArray) data = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	for (; *record_num < srec_records->len; *record_num += 1) {
		FuSrecFirmwareRecord *rcd = g_ptr_array_index(srec_records, *record_num);
		GByteArray *src = rcd->buf;

		/* skip non-data records */
		if (!(rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S1_DATA_16 ||
		      rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S2_DATA_24 ||
		      rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32))
			continue;

		/* initialize, if necessary */
		if (!base_addr) {
			base_addr = rcd->addr;
			expect_addr = rcd->addr;
		}

		/* Stop appending data to this block if we've reached an
		 * address discontinuity */
		if (rcd->addr != expect_addr)
			break;

		/* Stop appending data to this block if we've run out of
		 * available space */
		if (data->len + src->len > FU_WAC_MODULE_SUB_CPU_PAYLOAD_SZ) {
			if (data->len == 0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "record too big for a single block");
				return NULL;
			}
			break;
		}

		/* copy data into this block and prepare for next iteration */
		g_byte_array_append(data, src->data, src->len);
		expect_addr += src->len;
	}

	blob = g_bytes_new(data->data, data->len);
	chunk = fu_chunk_bytes_new(g_steal_pointer(&blob));
	fu_chunk_set_address(chunk, base_addr);
	return chunk;
}

static GPtrArray *
fu_wac_module_sub_cpu_parse_chunks(FuSrecFirmware *srec_firmware, guint32 *data_len, GError **error)
{
	GPtrArray *chunks = g_ptr_array_new_with_free_func(g_free);
	guint record_num = 0;
	GPtrArray *records = fu_srec_firmware_get_records(srec_firmware);

	*data_len = 0;
	while (record_num < records->len) {
		g_autofree FuChunk *chunk =
		    fu_wac_module_sub_cpu_create_chunk(records, &record_num, error);
		if (chunk == NULL)
			return NULL;
		*data_len += fu_chunk_get_data_sz(chunk);

		g_ptr_array_add(chunks, g_steal_pointer(&chunk));
	}

	return chunks;
}

static GBytes *
fu_wac_module_sub_cpu_build_packet(FuChunk *chunk, GError **error)
{
	guint8 buf[FU_WAC_MODULE_SUB_CPU_PAYLOAD_SZ + 5]; /* nocheck:zero-init */

	memset(buf, 0xff, sizeof(buf));
	fu_memwrite_uint32(&buf[0], fu_chunk_get_address(chunk), G_BIG_ENDIAN);
	buf[4] = fu_chunk_get_data_sz(chunk) / 2;
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    5, /* dst */
			    fu_chunk_get_data(chunk),
			    fu_chunk_get_data_sz(chunk),
			    0, /* src */
			    fu_chunk_get_data_sz(chunk),
			    error)) {
		g_prefix_error(error, "wacom sub_cpu module failed to build packet: ");
		return NULL;
	}

	return g_bytes_new(buf, sizeof(buf));
}

static gboolean
fu_wac_module_sub_cpu_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	guint8 buf_start[4] = {};
	guint32 firmware_len = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GBytes) blob_start = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);

	/* build each data packet */
	chunks =
	    fu_wac_module_sub_cpu_parse_chunks(FU_SREC_FIRMWARE(firmware), &firmware_len, error);
	if (chunks == NULL)
		return FALSE;

	/* build start command */
	fu_memwrite_uint32(buf_start, firmware_len, G_LITTLE_ENDIAN);
	blob_start = g_bytes_new_static(buf_start, sizeof(buf_start));

	/* start, which will erase the module */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_START,
				       blob_start,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_POLL_INTERVAL,
				       FU_WAC_MODULE_START_TIMEOUT,
				       error)) {
		g_prefix_error(error, "wacom sub_cpu module failed to erase: ");
		return FALSE;
	}

	fu_progress_step_done(progress);

	/* data */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chunk = g_ptr_array_index(chunks, i);
		g_autoptr(GBytes) blob_chunk = fu_wac_module_sub_cpu_build_packet(chunk, error);

		if (blob_chunk == NULL)
			return FALSE;

		if (!fu_wac_module_set_feature(self,
					       FU_WAC_MODULE_COMMAND_DATA,
					       blob_chunk,
					       fu_progress_get_child(progress),
					       FU_WAC_MODULE_POLL_INTERVAL,
					       FU_WAC_MODULE_DATA_TIMEOUT,
					       error)) {
			g_prefix_error(error, "wacom sub_cpu module failed to write: ");
			return FALSE;
		}
		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						i + 1,
						chunks->len);
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
		g_prefix_error(error, "wacom sub_cpu module failed to end: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_wac_module_sub_cpu_prepare_firmware(FuDevice *self,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	FuFirmware *firmware_srec = fu_srec_firmware_new();
	if (!fu_firmware_parse_stream(firmware_srec,
				      stream,
				      0,
				      flags | FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
				      error)) {
		g_prefix_error(error, "wacom sub_cpu failed to parse firmware: ");
		return NULL;
	}
	return firmware_srec;
}

static void
fu_wac_module_sub_cpu_init(FuWacModuleSubCpu *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration(FU_DEVICE(self), 15);
}

static void
fu_wac_module_sub_cpu_class_init(FuWacModuleSubCpuClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_wac_module_sub_cpu_write_firmware;
	device_class->prepare_firmware = fu_wac_module_sub_cpu_prepare_firmware;
}

FuWacModule *
fu_wac_module_sub_cpu_new(FuDevice *proxy)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_SUB_CPU,
			      "proxy",
			      proxy,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_SUB_CPU,
			      NULL);
	return module;
}
