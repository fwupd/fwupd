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

struct _FuWacModuleTouchId7 {
	FuWacModule parent_instance;
};

typedef struct {
	guint32 op_id;
	const guint8 *data;
	gsize len;
} WtaInfo;

typedef struct {
	guint32 headerSize;
	guint16 firmwareNumber;
} WtaFileHeader;

typedef struct {
	guint32 fileNameLength;
	guint32 startAddress;
	guint8 IC_ID;
	guint8 MA_ID;
	guint32 blockCount;
} WtaRecordHeader;

typedef struct {
	guint32 blockStart;
	guint32 blockSize;
} WtaBlockHeader;

G_DEFINE_TYPE(FuWacModuleTouchId7, fu_wac_module_touch_id7, FU_TYPE_WAC_MODULE)

/**
 * fu_wac_module_touch_id7_read_file_header:
 * @header: A #WtaFileHeader
 * @info: A #WtaInfo
 *
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
static void
fu_wac_module_touch_id7_read_file_header(WtaFileHeader *header, WtaInfo *info)
{
	info->data += 4;

	header->headerSize = fu_memread_uint32(info->data, G_LITTLE_ENDIAN);
	info->data += header->headerSize - 8;

	header->firmwareNumber = fu_memread_uint16(info->data, G_LITTLE_ENDIAN);
	info->data += 16;
}

/**
 * fu_wac_module_touch_id7_read_record_header:
 * @header: A #WtaRecordHeader
 * @info: A #WtaInfo
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
static void
fu_wac_module_touch_id7_read_record_header(WtaRecordHeader *header, WtaInfo *info)
{
	header->fileNameLength = fu_memread_uint32(info->data, G_LITTLE_ENDIAN);
	info->data += header->fileNameLength + 8;

	header->startAddress = fu_memread_uint32(info->data, G_LITTLE_ENDIAN);
	info->data += 8;

	header->IC_ID = info->data[0];
	info->data += 1;

	header->MA_ID = info->data[0];
	info->data += 3;

	header->blockCount = fu_memread_uint32(info->data, G_LITTLE_ENDIAN);
	info->data += 4;
}

/**
 * fu_wac_module_touch_id7_read_block_header:
 * @header: A #WtaBlockHeader
 * @info: A #WtaInfo
 *
 * Read and advance past a WTA block header.
 *
 * Block Header format:
 * {
 *   u32:    Start Address
 *   u32:    Data Block Size
 * }
 *
 */
static void
fu_wac_module_touch_id7_read_block_header(WtaBlockHeader *header, WtaInfo *info)
{
	header->blockStart = fu_memread_uint32(info->data, G_LITTLE_ENDIAN);
	info->data += 4;

	header->blockSize = fu_memread_uint32(info->data, G_LITTLE_ENDIAN);
	info->data += 4;
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
	buf[1] = header->IC_ID;
	buf[2] = header->MA_ID;
	fu_memwrite_uint32(&buf[3], op_id, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(&buf[7], header->startAddress, G_LITTLE_ENDIAN);
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
	WtaBlockHeader block_hdr;

	/* generate chunks off of the raw block data */
	fu_wac_module_touch_id7_read_block_header(&block_hdr, info);
	chunks = fu_chunk_array_new(info->data,
			       block_hdr.blockSize,
			       block_hdr.blockStart,
			       0x0,  /* page_sz */
			       FU_WAC_MODULE_CHUNK_SIZE); /* packet_sz */

	/* write data */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf[11 + FU_WAC_MODULE_CHUNK_SIZE];
		g_autoptr(GBytes) blob_chunk = NULL;

		buf[0] = FU_WAC_MODULE_COMMAND_DATA;
		buf[1] = record_hdr->IC_ID;
		buf[2] = record_hdr->MA_ID;
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
							info->len / FU_WAC_MODULE_CHUNK_SIZE + 10);
	}

	/* incrementing data to the next block */
	info->data += block_hdr.blockSize;

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
	WtaRecordHeader record_hdr;
	g_autoptr(GBytes) startSubBlob = NULL;
	g_autoptr(GBytes) endSubBlob = NULL;
	guint8 command[11];

	fu_wac_module_touch_id7_read_record_header(&record_hdr, info);

	/* start firmware record command */
	fu_wac_module_touch_id7_generate_command(&record_hdr,
						 FU_WAC_MODULE_COMMAND_START,
						 info->op_id,
						 command);
	startSubBlob = g_bytes_new(command, sizeof(command));
	if (!fu_wac_module_set_feature(self,
					FU_WAC_MODULE_COMMAND_DATA,
					startSubBlob,
					fu_progress_get_child(progress),
					FU_WAC_MODULE_ERASE_TIMEOUT,
					error))
		return FALSE;

	info->op_id++;

	/* write each block */
	for (guint32 i = 0; i < record_hdr.blockCount; i++) {
		if (!fu_wac_module_touch_id7_write_block(self, info, progress, &record_hdr, error))
			return FALSE;
	}

	/* end firmware record command */
	fu_wac_module_touch_id7_generate_command(&record_hdr,
						 FU_WAC_MODULE_COMMAND_END,
						 info->op_id,
						 command);

	endSubBlob = g_bytes_new(command, sizeof(command));
	if (!fu_wac_module_set_feature(self,
					FU_WAC_MODULE_COMMAND_DATA,
					endSubBlob,
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
	g_autoptr(GBytes) fwRead = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	WtaInfo info;
	WtaFileHeader file_hdr;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	g_debug("using element at addr 0x%0x", (guint)fu_firmware_get_addr(firmware));

	fwRead = fu_firmware_get_bytes(firmware, error);
	if (fwRead == NULL)
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
	info.data = g_bytes_get_data(fwRead, &info.len);
	info.op_id = 1;

	fu_wac_module_touch_id7_read_file_header(&file_hdr, &info);

	/* write each firmware record */
	for (int x = 0; x < file_hdr.firmwareNumber; x++) {
		if (!fu_wac_module_touch_id7_write_record(self, &info, progress, error))
			return FALSE;

		/* increment data to the next firmware record */
		info.data += 14;
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
