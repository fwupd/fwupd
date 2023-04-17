/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2023 Joshua Dickens <joshua.dickens@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-wac-device.h"
#include "fu-wac-module-touch-id7.h"
#include "fu-wac-struct.h"

struct _FuWacModuleTouchId7 {
	FuWacModule parent_instance;
};

typedef struct {
	guint32 op_id;
	const guint8 *buf;
	gsize bufsz;
	gsize offset;
} WtaInfo;

typedef struct {
	guint32 header_size;
	guint16 firmware_number;
} WtaFileHeader;

typedef struct {
	guint32 file_name_length;
	guint32 start_address;
	guint8 ic_id;
	guint8 ma_id;
	guint32 block_count;
} WtaRecordHeader;

G_DEFINE_TYPE(FuWacModuleTouchId7, fu_wac_module_touch_id7, FU_TYPE_WAC_MODULE)

/**
 * Read and advance past a WTA file header.
 *
 * File Header format:
 * {
 *   u32:    Starting symbol for the file (WTA)
 *   u32:    Header Size
 *   u8[]:   Variable-length padding to bring the header to match Header Size
 *   u16:    Number of Firmware
 *   u8[]:   Padding/Unnecessary Data
 * }
 *
 */
static gboolean
fu_wac_module_touch_id7_read_file_header(WtaFileHeader *header, WtaInfo *info, GError **error)
{
	info->offset += 4;

	if (!fu_memread_uint32_safe(info->buf,
				    info->bufsz,
				    info->offset,
				    &header->header_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	info->offset += header->header_size - 8;

	if (!fu_memread_uint16_safe(info->buf,
				    info->bufsz,
				    info->offset,
				    &header->firmware_number,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	info->offset += 16;
	return TRUE;
}

/**
 * fu_wac_module_touch_id7_read_record_header:
 *
 * Read and advance past a WTA record header.
 *
 * Header format:
 * {
 *   u32:    Length of filename
 *   char[]: Variable-length null-terminated filename string
 *   u8[]:   Variable-length padding to bring filename to a multiple of 4 bytes
 *   u8:     Firmware Type
 *   u8[]:   3 Bytes padding to bring Firmware Type to a multiple of 4 bytes
 *   u32:    Start address
 *   u32:    Segment Size
 *   u8:     IC_ID
 *   u8:     MA_ID
 *   u8[]:   2 Bytes padding to bring IC_ID/MA_ID to a multiple of 4 bytes
 *   u32:    Block Count
 * }
 *
 */
static gboolean
fu_wac_module_touch_id7_read_record_header(WtaRecordHeader *header, WtaInfo *info, GError **error)
{
	if (!fu_memread_uint32_safe(info->buf,
				    info->bufsz,
				    info->offset,
				    &header->file_name_length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	info->offset += header->file_name_length + 8;

	if (!fu_memread_uint32_safe(info->buf,
				    info->bufsz,
				    info->offset,
				    &header->start_address,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	info->offset += 8;

	if (!fu_memread_uint8_safe(info->buf, info->bufsz, info->offset, &header->ic_id, error))
		return FALSE;
	info->offset += 1;

	if (!fu_memread_uint8_safe(info->buf, info->bufsz, info->offset, &header->ma_id, error))
		return FALSE;
	info->offset += 3;

	if (!fu_memread_uint32_safe(info->buf,
				    info->bufsz,
				    info->offset,
				    &header->block_count,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	info->offset += 4;

	/* success */
	return TRUE;
}

/**
 * fu_wac_module_touch_id7_generate_command:
 * @header: A #WtaRecordHeader
 * @cmd: The type of command to be sent
 * @op_id: The operation serial number
 * @buf: The buffer to write the command into
 *
 * Generate a standard touch id7 command preamble.
 *
 */
static void
fu_wac_module_touch_id7_generate_command(const WtaRecordHeader *header,
					 guint8 cmd,
					 guint16 op_id,
					 guint8 *buf)
{
	buf[0] = cmd;
	buf[1] = header->ic_id;
	buf[2] = header->ma_id;
	fu_memwrite_uint32(&buf[3], op_id, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(&buf[7], header->start_address, G_LITTLE_ENDIAN);
}

/**
 * fu_wac_module_touch_id7_write_block:
 *
 * Write the data of a single firmware block to the device.
 *
 */
static gboolean
fu_wac_module_touch_id7_write_block(FuWacModule *self,
				    WtaInfo *info,
				    FuProgress *progress,
				    WtaRecordHeader *record_hdr,
				    GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) st_blk = NULL;

	/* generate chunks off of the raw block data */
	st_blk = fu_struct_wta_block_header_parse(info->buf, info->bufsz, info->offset, error);
	if (st_blk == NULL)
		return FALSE;
	info->offset += FU_STRUCT_WTA_BLOCK_HEADER_SIZE;
	chunks = fu_chunk_array_new(info->buf + info->offset,
				    fu_struct_wta_block_header_get_block_size(st_blk),
				    fu_struct_wta_block_header_get_block_start(st_blk),
				    0x0,		       /* page_sz */
				    FU_WAC_MODULE_CHUNK_SIZE); /* packet_sz */

	/* write data */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf[11 + FU_WAC_MODULE_CHUNK_SIZE];
		g_autoptr(GBytes) blob_chunk = NULL;

		buf[0] = FU_WAC_MODULE_COMMAND_DATA;
		buf[1] = record_hdr->ic_id;
		buf[2] = record_hdr->ma_id;
		fu_memwrite_uint32(&buf[3], info->op_id, G_LITTLE_ENDIAN);
		fu_memwrite_uint32(&buf[7], fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		memcpy(&buf[11], fu_chunk_get_data(chk), FU_WAC_MODULE_CHUNK_SIZE);
		blob_chunk = g_bytes_new(buf, sizeof(buf));
		if (!fu_wac_module_set_feature(self,
					       FU_WAC_MODULE_COMMAND_DATA,
					       blob_chunk,
					       fu_progress_get_child(progress),
					       FU_WAC_MODULE_WRITE_TIMEOUT,
					       error)) {
			g_prefix_error(error, "failed to write block %u: ", info->op_id);
			return FALSE;
		}

		info->op_id++;

		/* rough estimate based on file size with some added to handle the extra firmware
		 * record start and end commands */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						info->op_id,
						info->bufsz / FU_WAC_MODULE_CHUNK_SIZE + 10);
	}

	/* incrementing data to the next block */
	info->offset += fu_struct_wta_block_header_get_block_size(st_blk);

	return TRUE;
}

/**
 * fu_wac_module_touch_id7_write_record:
 *
 * Start and end the write process for a single touch id7 firmware record and
 * the block(s) it contains.
 * A touch id7 firmware record acts as it's own mini update with the device,
 * with a start and end command each individual record.
 * A single touch id7 firmware record can contain one or more blocks that have
 * the raw data for writing.
 */
static gboolean
fu_wac_module_touch_id7_write_record(FuWacModule *self,
				     WtaInfo *info,
				     FuProgress *progress,
				     GError **error)
{
	WtaRecordHeader record_hdr = {0x0};
	g_autoptr(GBytes) blob_start = NULL;
	g_autoptr(GBytes) blob_end = NULL;
	guint8 command[11];

	if (!fu_wac_module_touch_id7_read_record_header(&record_hdr, info, error))
		return FALSE;

	/* start firmware record command */
	fu_wac_module_touch_id7_generate_command(&record_hdr,
						 FU_WAC_MODULE_COMMAND_START,
						 info->op_id,
						 command);
	blob_start = g_bytes_new(command, sizeof(command));
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_DATA,
				       blob_start,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_ERASE_TIMEOUT,
				       error))
		return FALSE;

	info->op_id++;

	/* write each block */
	for (guint32 i = 0; i < record_hdr.block_count; i++) {
		if (!fu_wac_module_touch_id7_write_block(self, info, progress, &record_hdr, error))
			return FALSE;
	}

	/* end firmware record command */
	fu_wac_module_touch_id7_generate_command(&record_hdr,
						 FU_WAC_MODULE_COMMAND_END,
						 info->op_id,
						 command);

	blob_end = g_bytes_new(command, sizeof(command));
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_DATA,
				       blob_end,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_ERASE_TIMEOUT,
				       error))
		return FALSE;

	info->op_id++;

	return TRUE;
}

/**
 * fu_wac_module_touch_id7_write_firmware:
 *
 * Start and End the overall update process for touch id7 firmware and the
 * record(s) it contains.
 * A touch id7 firmware will usually contain 3 firmware record(s) but could
 * potentially have less or more.
 */
static gboolean
fu_wac_module_touch_id7_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	g_autoptr(GBytes) blob = NULL;
	WtaInfo info;
	WtaFileHeader file_hdr;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	g_debug("using element at addr 0x%0x", (guint)fu_firmware_get_addr(firmware));

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* start, which will erase the module */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_START,
				       NULL,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_ERASE_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* set basic info */
	info.offset = 0x0;
	info.buf = g_bytes_get_data(blob, &info.bufsz);
	info.op_id = 1;
	if (!fu_wac_module_touch_id7_read_file_header(&file_hdr, &info, error))
		return FALSE;

	/* write each firmware record */
	for (guint i = 0; i < file_hdr.firmware_number; i++) {
		if (!fu_wac_module_touch_id7_write_record(self, &info, progress, error))
			return FALSE;

		/* increment data to the next firmware record */
		info.offset += 14;
	}
	fu_progress_step_done(progress);

	/* end */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_END,
				       NULL,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_FINISH_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_wac_module_touch_id7_init(FuWacModuleTouchId7 *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration(FU_DEVICE(self), 90);
}

static void
fu_wac_module_touch_id7_class_init(FuWacModuleTouchId7Class *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_wac_module_touch_id7_write_firmware;
}

FuWacModule *
fu_wac_module_touch_id7_new(FuDevice *proxy)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_TOUCH_ID7,
			      "proxy",
			      proxy,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_TOUCH_ID7,
			      NULL);
	return module;
}
