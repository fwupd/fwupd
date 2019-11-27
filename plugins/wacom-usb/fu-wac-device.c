/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-wac-device.h"
#include "fu-wac-common.h"
#include "fu-wac-firmware.h"
#include "fu-wac-module-bluetooth.h"
#include "fu-wac-module-touch.h"

typedef struct __attribute__((packed)) {
	guint32		 start_addr;
	guint32		 block_sz;
	guint16		 write_sz; /* bit 15 is write protection flag */
} FuWacFlashDescriptor;

typedef enum {
	FU_WAC_STATUS_UNKNOWN			= 0,
	FU_WAC_STATUS_WRITING			= 1 << 0,
	FU_WAC_STATUS_ERASING			= 1 << 1,
	FU_WAC_STATUS_ERROR_WRITE		= 1 << 2,
	FU_WAC_STATUS_ERROR_ERASE		= 1 << 3,
	FU_WAC_STATUS_WRITE_PROTECTED		= 1 << 4,
	FU_WAC_STATUS_LAST
} FuWacStatus;

#define FU_WAC_DEVICE_TIMEOUT			5000	/* ms */

struct _FuWacDevice
{
	FuUsbDevice		 parent_instance;
	GPtrArray		*flash_descriptors;
	GArray			*checksums;
	guint32			 status_word;
	guint16			 firmware_index;
	guint16			 loader_ver;
	guint16			 read_data_sz;
	guint16			 write_word_sz;
	guint16			 write_block_sz;	/* usb transfer size */
	guint16			 nr_flash_blocks;
	guint16			 configuration;
};

G_DEFINE_TYPE (FuWacDevice, fu_wac_device, FU_TYPE_USB_DEVICE)

static GString *
fu_wac_device_status_to_string (guint32 status_word)
{
	GString *str = g_string_new (NULL);
	if (status_word & FU_WAC_STATUS_WRITING)
		g_string_append (str, "writing,");
	if (status_word & FU_WAC_STATUS_ERASING)
		g_string_append (str, "erasing,");
	if (status_word & FU_WAC_STATUS_ERROR_WRITE)
		g_string_append (str, "error-write,");
	if (status_word & FU_WAC_STATUS_ERROR_ERASE)
		g_string_append (str, "error-erase,");
	if (status_word & FU_WAC_STATUS_WRITE_PROTECTED)
		g_string_append (str, "write-protected,");
	if (str->len == 0) {
		g_string_append (str, "none");
		return str;
	}
	g_string_truncate (str, str->len - 1);
	return str;
}

static gboolean
fu_wav_device_flash_descriptor_is_wp (const FuWacFlashDescriptor *fd)
{
	return fd->write_sz & 0x8000;
}

static void
fu_wac_device_flash_descriptor_to_string (FuWacFlashDescriptor *fd, guint idt, GString *str)
{
	fu_common_string_append_kx (str, idt, "StartAddr", fd->start_addr);
	fu_common_string_append_kx (str, idt, "BlockSize", fd->block_sz);
	fu_common_string_append_kx (str, idt, "WriteSize", fd->write_sz & ~0x8000);
	fu_common_string_append_kb (str, idt, "Protected",
				    fu_wav_device_flash_descriptor_is_wp (fd));
}

static void
fu_wac_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuWacDevice *self = FU_WAC_DEVICE (device);
	g_autoptr(GString) status_str = NULL;

	if (self->firmware_index != 0xffff) {
		g_autofree gchar *tmp = g_strdup_printf ("0x%04x", self->firmware_index);
		fu_common_string_append_kv (str, idt, "FwIndex", tmp);
	}
	if (self->loader_ver > 0) {
		g_autofree gchar *tmp = g_strdup_printf ("0x%04x", (guint) self->loader_ver);
		fu_common_string_append_kv (str, idt, "LoaderVer", tmp);
	}
	if (self->read_data_sz > 0) {
		g_autofree gchar *tmp = g_strdup_printf ("0x%04x", (guint) self->read_data_sz);
		fu_common_string_append_kv (str, idt, "ReadDataSize", tmp);
	}
	if (self->write_word_sz > 0) {
		g_autofree gchar *tmp = g_strdup_printf ("0x%04x", (guint) self->write_word_sz);
		fu_common_string_append_kv (str, idt, "WriteWordSize", tmp);
	}
	if (self->write_block_sz > 0) {
		g_autofree gchar *tmp = g_strdup_printf ("0x%04x", (guint) self->write_block_sz);
		fu_common_string_append_kv (str, idt, "WriteBlockSize", tmp);
	}
	if (self->nr_flash_blocks > 0) {
		g_autofree gchar *tmp = g_strdup_printf ("0x%04x", (guint) self->nr_flash_blocks);
		fu_common_string_append_kv (str, idt, "NrFlashBlocks", tmp);
	}
	if (self->configuration != 0xffff) {
		g_autofree gchar *tmp = g_strdup_printf ("0x%04x", (guint) self->configuration);
		fu_common_string_append_kv (str, idt, "Configuration", tmp);
	}
	for (guint i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index (self->flash_descriptors, i);
		g_autofree gchar *title = g_strdup_printf ("FlashDescriptor%02u", i);
		fu_common_string_append_kv (str, idt, title, NULL);
		fu_wac_device_flash_descriptor_to_string (fd, idt + 1, str);
	}
	status_str = fu_wac_device_status_to_string (self->status_word);
	fu_common_string_append_kv (str, idt, "Status", status_str->str);
}

gboolean
fu_wac_device_get_feature_report (FuWacDevice *self,
				  guint8 *buf, gsize bufsz,
				  FuWacDeviceFeatureFlags flags,
				  GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize sz = 0;
	guint8 cmd = buf[0];

	/* hit hardware */
	if ((flags & FU_WAC_DEVICE_FEATURE_FLAG_NO_DEBUG) == 0)
		fu_wac_buffer_dump ("GET", cmd, buf, bufsz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_GET,		/* bRequest */
					    FU_HID_FEATURE | cmd,		/* wValue */
					    0x0000,			/* wIndex */
					    buf, bufsz, &sz,
					    FU_WAC_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "Failed to get feature report: ");
		return FALSE;
	}
	if ((flags & FU_WAC_DEVICE_FEATURE_FLAG_NO_DEBUG) == 0)
		fu_wac_buffer_dump ("GE2", cmd, buf, sz);

	/* check packet */
	if ((flags & FU_WAC_DEVICE_FEATURE_FLAG_ALLOW_TRUNC) == 0 && sz != bufsz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "packet get bytes %" G_GSIZE_FORMAT
			     " expected %" G_GSIZE_FORMAT,
			     sz, bufsz);
		return FALSE;
	}
	if (buf[0] != cmd) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "command response was %i expected %i",
			     buf[0], cmd);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_wac_device_set_feature_report (FuWacDevice *self,
				  guint8 *buf, gsize bufsz,
				  FuWacDeviceFeatureFlags flags,
				  GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize sz = 0;
	guint8 cmd = buf[0];

	/* hit hardware */
	fu_wac_buffer_dump ("SET", cmd, buf, bufsz);
	if (g_getenv ("FWUPD_WAC_EMULATE") != NULL)
		return TRUE;
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET,		/* bRequest */
					    FU_HID_FEATURE | cmd,		/* wValue */
					    0x0000,			/* wIndex */
					    buf, bufsz, &sz,
					    FU_WAC_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "Failed to set feature report: ");
		return FALSE;
	}

	/* check packet */
	if ((flags & FU_WAC_DEVICE_FEATURE_FLAG_ALLOW_TRUNC) == 0 && sz != bufsz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "packet sent bytes %" G_GSIZE_FORMAT
			     " expected %" G_GSIZE_FORMAT,
			     sz, bufsz);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wac_device_ensure_flash_descriptors (FuWacDevice *self, GError **error)
{
	gsize sz = (self->nr_flash_blocks * 10) + 1;
	g_autofree guint8 *buf = NULL;

	/* already done */
	if (self->flash_descriptors->len > 0)
		return TRUE;

	/* hit hardware */
	buf = g_malloc (sz);
	memset (buf, 0xff, sz);
	buf[0] = FU_WAC_REPORT_ID_GET_FLASH_DESCRIPTOR;
	if (!fu_wac_device_get_feature_report (self, buf, sz,
					       FU_WAC_DEVICE_FEATURE_FLAG_NONE,
					       error))
		return FALSE;

	/* parse */
	for (guint i = 0; i < self->nr_flash_blocks; i++) {
		FuWacFlashDescriptor *fd = g_new0 (FuWacFlashDescriptor, 1);
		const guint blksz = sizeof(FuWacFlashDescriptor);
		fd->start_addr = fu_common_read_uint32 (buf + (i * blksz) + 1, G_LITTLE_ENDIAN);
		fd->block_sz = fu_common_read_uint32 (buf + (i * blksz) + 5, G_LITTLE_ENDIAN);
		fd->write_sz = fu_common_read_uint16 (buf + (i * blksz) + 9, G_LITTLE_ENDIAN);
		g_ptr_array_add (self->flash_descriptors, fd);
	}
	g_debug ("added %u flash descriptors", self->flash_descriptors->len);
	return TRUE;
}

static gboolean
fu_wac_device_ensure_status (FuWacDevice *self, GError **error)
{
	g_autoptr(GString) str = NULL;
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_GET_STATUS,
			 [1 ... 4] = 0xff };

	/* hit hardware */
	buf[0] = FU_WAC_REPORT_ID_GET_STATUS;
	if (!fu_wac_device_get_feature_report (self, buf, sizeof(buf),
					       FU_WAC_DEVICE_FEATURE_FLAG_NONE,
					       error))
		return FALSE;

	/* parse */
	self->status_word = fu_common_read_uint32 (buf + 1, G_LITTLE_ENDIAN);
	str = fu_wac_device_status_to_string (self->status_word);
	g_debug ("status now: %s", str->str);
	return TRUE;
}

static gboolean
fu_wac_device_ensure_checksums (FuWacDevice *self, GError **error)
{
	gsize sz = (self->nr_flash_blocks * 4) + 5;
	guint32 updater_version;
	g_autofree guint8 *buf = g_malloc (sz);

	/* hit hardware */
	memset (buf, 0xff, sz);
	buf[0] = FU_WAC_REPORT_ID_GET_CHECKSUMS;
	if (!fu_wac_device_get_feature_report (self, buf, sz,
					       FU_WAC_DEVICE_FEATURE_FLAG_NONE,
					       error))
		return FALSE;

	/* parse */
	updater_version = fu_common_read_uint32 (buf + 1, G_LITTLE_ENDIAN);
	g_debug ("updater-version: %" G_GUINT32_FORMAT, updater_version);

	/* get block checksums */
	g_array_set_size (self->checksums, 0);
	for (guint i = 0; i < self->nr_flash_blocks; i++) {
		guint32 csum = fu_common_read_uint32 (buf + 5 + (i * 4), G_LITTLE_ENDIAN);
		g_debug ("checksum block %02u: 0x%08x", i, (guint) csum);
		g_array_append_val (self->checksums, csum);
	}
	g_debug ("added %u checksums", self->flash_descriptors->len);

	return TRUE;
}


static gboolean
fu_wac_device_ensure_firmware_index (FuWacDevice *self, GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_GET_CURRENT_FIRMWARE_IDX,
			 [1 ... 2] = 0xff };

	/* hit hardware */
	if (!fu_wac_device_get_feature_report (self, buf, sizeof(buf),
					       FU_WAC_DEVICE_FEATURE_FLAG_NONE,
					       error))
		return FALSE;

	/* parse */
	self->firmware_index = fu_common_read_uint16 (buf + 1, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_wac_device_ensure_parameters (FuWacDevice *self, GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_GET_PARAMETERS,
			 [1 ... 12] = 0xff };

	/* hit hardware */
	if (!fu_wac_device_get_feature_report (self, buf, sizeof(buf),
					       FU_WAC_DEVICE_FEATURE_FLAG_NONE,
					       error))
		return FALSE;

	/* parse */
	self->loader_ver = fu_common_read_uint16 (buf + 1, G_LITTLE_ENDIAN);
	self->read_data_sz = fu_common_read_uint16 (buf + 3, G_LITTLE_ENDIAN);
	self->write_word_sz = fu_common_read_uint16 (buf + 5, G_LITTLE_ENDIAN);
	self->write_block_sz = fu_common_read_uint16 (buf + 7, G_LITTLE_ENDIAN);
	self->nr_flash_blocks = fu_common_read_uint16 (buf + 9, G_LITTLE_ENDIAN);
	self->configuration = fu_common_read_uint16 (buf + 11, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_wac_device_write_block (FuWacDevice *self,
			   guint32 addr,
			   GBytes *blob,
			   GError **error)
{
	const guint8 *tmp;
	gsize bufsz = self->write_block_sz + 5;
	gsize sz = 0;
	g_autofree guint8 *buf = NULL;

	/* check size */
	tmp = g_bytes_get_data (blob, &sz);
	if (sz > self->write_block_sz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "packet was too large at %" G_GSIZE_FORMAT " bytes",
			     sz);
		return FALSE;
	}

	/* build packet */
	buf = g_malloc (bufsz);
	memset (buf, 0xff, bufsz);
	buf[0] = FU_WAC_REPORT_ID_WRITE_BLOCK;
	fu_common_write_uint32 (buf + 1, addr, G_LITTLE_ENDIAN);
	if (sz > 0) {
		if (!fu_memcpy_safe (buf, bufsz, 0x5,	/* dst */
				     tmp, sz, 0x0,	/* src */
				     sz, error))
			return FALSE;
	}

	/* hit hardware */
	return fu_wac_device_set_feature_report (self, buf, bufsz,
						 FU_WAC_DEVICE_FEATURE_FLAG_NONE,
						 error);
}

static gboolean
fu_wac_device_erase_block (FuWacDevice *self, guint32 addr, GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_ERASE_BLOCK,
			 [1 ... 4] = 0xff };

	/* build packet */
	fu_common_write_uint32 (buf + 1, addr, G_LITTLE_ENDIAN);

	/* hit hardware */
	return fu_wac_device_set_feature_report (self, buf, sizeof(buf),
						 FU_WAC_DEVICE_FEATURE_FLAG_NONE,
						 error);
}

gboolean
fu_wac_device_update_reset (FuWacDevice *self, GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_UPDATE_RESET,
			 [1 ... 4] = 0xff };

	/* hit hardware */
	return fu_wac_device_set_feature_report (self, buf, sizeof(buf),
						 FU_WAC_DEVICE_FEATURE_FLAG_NONE,
						 error);
}

static gboolean
fu_wac_device_set_checksum_of_block (FuWacDevice *self,
				     guint16 block_nr,
				     guint32 checksum,
				     GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_SET_CHECKSUM_FOR_BLOCK,
			 [1 ... 6] = 0xff };

	/* build packet */
	fu_common_write_uint16 (buf + 1, block_nr, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf + 3, checksum, G_LITTLE_ENDIAN);

	/* hit hardware */
	return fu_wac_device_set_feature_report (self, buf, sizeof(buf),
						 FU_WAC_DEVICE_FEATURE_FLAG_NONE,
						 error);
}

static gboolean
fu_wac_device_calculate_checksum_of_block (FuWacDevice *self,
					   guint16 block_nr,
					   GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_CALCULATE_CHECKSUM_FOR_BLOCK,
			 [1 ... 2] = 0xff };

	/* build packet */
	fu_common_write_uint16 (buf + 1, block_nr, G_LITTLE_ENDIAN);

	/* hit hardware */
	return fu_wac_device_set_feature_report (self, buf, sizeof(buf),
						 FU_WAC_DEVICE_FEATURE_FLAG_NONE,
						 error);
}

static gboolean
fu_wac_device_write_checksum_table (FuWacDevice *self, GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_WRITE_CHECKSUM_TABLE,
			 [1 ... 4] = 0xff };

	/* hit hardware */
	return fu_wac_device_set_feature_report (self, buf, sizeof(buf),
						 FU_WAC_DEVICE_FEATURE_FLAG_NONE,
						 error);
}

static gboolean
fu_wac_device_switch_to_flash_loader (FuWacDevice *self, GError **error)
{
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_SWITCH_TO_FLASH_LOADER,
			 [1] = 0x05,
			 [2] = 0x6a };

	/* hit hardware */
	return fu_wac_device_set_feature_report (self, buf, sizeof(buf),
						 FU_WAC_DEVICE_FEATURE_FLAG_NONE,
						 error);
}

static FuFirmware *
fu_wac_device_prepare_firmware (FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_wac_firmware_new ();
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_wac_device_write_firmware (FuDevice *device,
			      FuFirmware *firmware,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuWacDevice *self = FU_WAC_DEVICE (device);
	gsize blocks_done = 0;
	gsize blocks_total = 0;
	g_autofree guint32 *csum_local = NULL;
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GHashTable) fd_blobs = NULL;

	/* use the correct image from the firmware */
	img = fu_firmware_get_image_by_idx (firmware, self->firmware_index == 1 ? 1 : 0, error);
	if (img == NULL)
		return FALSE;
	g_debug ("using image at addr 0x%0x", (guint) fu_firmware_image_get_addr (img));

	/* enter flash mode */
	if (!fu_wac_device_switch_to_flash_loader (self, error))
		return FALSE;

	/* get current selected device */
	if (!fu_wac_device_ensure_firmware_index (self, error))
		return FALSE;

	/* get firmware parameters (page sz and transfer sz) */
	if (!fu_wac_device_ensure_parameters (self, error))
		return FALSE;

	/* get the current flash descriptors */
	if (!fu_wac_device_ensure_flash_descriptors (self, error))
		return FALSE;

	/* get the updater protocol version */
	if (!fu_wac_device_ensure_checksums (self, error))
		return FALSE;

	/* clear all checksums of pages */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	for (guint16 i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index (self->flash_descriptors, i);
		if (fu_wav_device_flash_descriptor_is_wp (fd))
			continue;
		if (!fu_wac_device_set_checksum_of_block (self, i, 0x0, error))
			return FALSE;
	}

	/* get the blobs for each chunk */
	fd_blobs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					  NULL, (GDestroyNotify) g_bytes_unref);
	for (guint16 i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index (self->flash_descriptors, i);
		GBytes *blob_block;
		g_autoptr(GBytes) blob_tmp = NULL;

		if (fu_wav_device_flash_descriptor_is_wp (fd))
			continue;
		blob_tmp = fu_firmware_image_write_chunk (img,
							  fd->start_addr,
							  fd->block_sz,
							  NULL);
		if (blob_tmp == NULL)
			break;
		blob_block = fu_common_bytes_pad (blob_tmp, fd->block_sz);
		g_hash_table_insert (fd_blobs, fd, blob_block);
	}

	/* checksum actions post-write */
	blocks_total = g_hash_table_size (fd_blobs) + 2;

	/* write the data into the flash page */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	csum_local = g_new0 (guint32, self->flash_descriptors->len);
	for (guint16 i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index (self->flash_descriptors, i);
		GBytes *blob_block;
		g_autoptr(GPtrArray) chunks = NULL;

		/* if page is protected */
		if (fu_wav_device_flash_descriptor_is_wp (fd))
			continue;

		/* get data for page */
		blob_block = g_hash_table_lookup (fd_blobs, fd);
		if (blob_block == NULL)
			break;

		/* ignore empty blocks */
		if (fu_common_bytes_is_empty (blob_block)) {
			g_debug ("empty block, ignoring");
			fu_device_set_progress_full (device, blocks_done++, blocks_total);
			continue;
		}

		/* erase entire block */
		if (!fu_wac_device_erase_block (self, i, error))
			return FALSE;

		/* write block in chunks */
		chunks = fu_chunk_array_new_from_bytes (blob_block,
							fd->start_addr,
							0, /* page_sz */
							self->write_block_sz);
		for (guint j = 0; j < chunks->len; j++) {
			FuChunk *chk = g_ptr_array_index (chunks, j);
			g_autoptr(GBytes) blob_chunk = g_bytes_new (chk->data, chk->data_sz);
			if (!fu_wac_device_write_block (self, chk->address, blob_chunk, error))
				return FALSE;
		}

		/* calculate expected checksum and save to device RAM */
		csum_local[i] = fu_wac_calculate_checksum32le_bytes (blob_block);
		g_debug ("block checksum %02u: 0x%08x", i, csum_local[i]);
		if (!fu_wac_device_set_checksum_of_block (self, i, csum_local[i], error))
			return FALSE;

		/* update device progress */
		fu_device_set_progress_full (device, blocks_done++, blocks_total);
	}

	/* calculate CRC inside device */
	for (guint16 i = 0; i < self->flash_descriptors->len; i++) {
		if (!fu_wac_device_calculate_checksum_of_block (self, i, error))
			return FALSE;
	}

	/* update device progress */
	fu_device_set_progress_full (device, blocks_done++, blocks_total);

	/* read all CRC of all pages and verify with local CRC */
	if (!fu_wac_device_ensure_checksums (self, error))
		return FALSE;
	for (guint16 i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index (self->flash_descriptors, i);
		GBytes *blob_block;
		guint32 csum_rom;

		/* if page is protected */
		if (fu_wav_device_flash_descriptor_is_wp (fd))
			continue;

		/* no more written pages */
		blob_block = g_hash_table_lookup (fd_blobs, fd);
		if (blob_block == NULL)
			continue;
		if (fu_common_bytes_is_empty (blob_block))
			continue;

		/* check checksum matches */
		csum_rom = g_array_index (self->checksums, guint32, i);
		if (csum_rom != csum_local[i]) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed local checksum at block %u, "
				     "got 0x%08x expected 0x%08x", i,
				     (guint) csum_rom, (guint) csum_local[i]);
			return FALSE;
		}
		g_debug ("matched checksum at block %u of 0x%08x", i, csum_rom);
	}

	/* update device progress */
	fu_device_set_progress_full (device, blocks_done++, blocks_total);

	/* store host CRC into flash */
	if (!fu_wac_device_write_checksum_table (self, error))
		return FALSE;

	/* update progress */
	fu_device_set_progress_full (device, blocks_total, blocks_total);
	return TRUE;
}

static gboolean
fu_wac_device_add_modules_bluetooth (FuWacDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuWacModule) module = NULL;
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_BLUETOOTH,
			 [1 ... 14] = 0xff };

	buf[0] = FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_BLUETOOTH;
	if (!fu_wac_device_get_feature_report (self, buf, sizeof(buf),
					       FU_WAC_DEVICE_FEATURE_FLAG_NONE,
					       error)) {
		g_prefix_error (error, "Failed to get GetFirmwareVersionBluetooth: ");
		return FALSE;
	}

	/* success */
	name = g_strdup_printf ("%s [Legacy Bluetooth Module]",
				fu_device_get_name (FU_DEVICE (self)));
	version = g_strdup_printf ("%x.%x", (guint) buf[2], (guint) buf[1]);
	module = fu_wac_module_bluetooth_new (usb_device);
	fu_device_add_child (FU_DEVICE (self), FU_DEVICE (module));
	fu_device_set_name (FU_DEVICE (module), name);
	fu_device_set_version (FU_DEVICE (module), version, FWUPD_VERSION_FORMAT_PAIR);
	return TRUE;
}

static gboolean
fu_wac_device_add_modules_legacy (FuWacDevice *self, GError **error)
{
	g_autoptr(GError) error_bt = NULL;

	/* optional bluetooth */
	if (!fu_wac_device_add_modules_bluetooth (self, &error_bt))
		g_debug ("no bluetooth hardware: %s", error_bt->message);

	return TRUE;
}

static gboolean
fu_wac_device_add_modules (FuWacDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	g_autofree gchar *version_bootloader = NULL;
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_FW_DESCRIPTOR,
			 [1 ... 31] = 0xff };

	if (!fu_wac_device_get_feature_report (self, buf, sizeof(buf),
					       FU_WAC_DEVICE_FEATURE_FLAG_NONE,
					       error)) {
		g_prefix_error (error, "Failed to get DeviceFirmwareDescriptor: ");
		return FALSE;
	}

	/* verify bootloader is compatible */
	if (buf[1] != 0x01) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "bootloader major version not compatible");
		return FALSE;
	}

	/* verify the number of submodules is possible */
	if (buf[3] > (512 - 4) / 4) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "number of submodules is impossible");
		return FALSE;
	}

	/* bootloader version */
	version_bootloader = g_strdup_printf ("%u.%u", buf[1], buf[2]);
	fu_device_set_version_bootloader (FU_DEVICE (self), version_bootloader);

	/* get versions of each submodule */
	for (guint8 i = 0; i < buf[3]; i++) {
		guint8 fw_type = buf[(i * 4) + 4] & ~0x80;
		g_autofree gchar *name = NULL;
		g_autofree gchar *version = NULL;
		g_autoptr(FuWacModule) module = NULL;

		/* version number is decimal */
		version = g_strdup_printf ("%u.%u", buf[(i * 4) + 5], buf[(i * 4) + 6]);

		switch (fw_type) {
		case FU_WAC_MODULE_FW_TYPE_TOUCH:
			module = fu_wac_module_touch_new (usb_device);
			name = g_strdup_printf ("%s [Touch Module]",
						fu_device_get_name (FU_DEVICE (self)));
			fu_device_add_child (FU_DEVICE (self), FU_DEVICE (module));
			fu_device_set_name (FU_DEVICE (module), name);
			fu_device_set_version (FU_DEVICE (module), version, FWUPD_VERSION_FORMAT_PAIR);
			break;
		case FU_WAC_MODULE_FW_TYPE_BLUETOOTH:
			module = fu_wac_module_bluetooth_new (usb_device);
			name = g_strdup_printf ("%s [Bluetooth Module]",
						fu_device_get_name (FU_DEVICE (self)));
			fu_device_add_child (FU_DEVICE (self), FU_DEVICE (module));
			fu_device_set_name (FU_DEVICE (module), name);
			fu_device_set_version (FU_DEVICE (module), version, FWUPD_VERSION_FORMAT_PAIR);
			break;
		case FU_WAC_MODULE_FW_TYPE_MAIN:
			fu_device_set_version (FU_DEVICE (self), version, FWUPD_VERSION_FORMAT_PAIR);
			break;
		default:
			g_warning ("unknown submodule type 0x%0x", fw_type);
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_wac_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* open device */
	if (!g_usb_device_claim_interface (usb_device, 0x00, /* HID */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim HID interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wac_device_setup (FuDevice *device, GError **error)
{
	FuWacDevice *self = FU_WAC_DEVICE (device);

	/* get current status */
	if (!fu_wac_device_ensure_status (self, error))
		return FALSE;

	/* get version of each sub-module */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION)) {
		if (!fu_wac_device_add_modules_legacy (self, error))
			return FALSE;
	} else {
		if (!fu_wac_device_add_modules (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wac_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* reattach wacom.ko */
	if (!g_usb_device_release_interface (usb_device, 0x00, /* HID */
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to re-attach interface: ");
		return FALSE;
	}

	/* The hidcore subsystem uses a generic power_supply that has a deferred
	 * work item that will lock the device. When removing the power_supply,
	 * we take the lock, then cancel the work item which needs to take the
	 * lock too. This needs to be fixed in the kernel, but for the moment
	 * this should let the kernel unstick itself. */
	g_usleep (20 * 1000);

	/* success */
	return TRUE;
}

static void
fu_wac_device_init (FuWacDevice *self)
{
	self->flash_descriptors = g_ptr_array_new_with_free_func (g_free);
	self->checksums = g_array_new (FALSE, FALSE, sizeof(guint32));
	self->configuration = 0xffff;
	self->firmware_index = 0xffff;
	fu_device_set_protocol (FU_DEVICE (self), "com.wacom.usb");
	fu_device_add_icon (FU_DEVICE (self), "input-tablet");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration (FU_DEVICE (self), 10);
}

static void
fu_wac_device_finalize (GObject *object)
{
	FuWacDevice *self = FU_WAC_DEVICE (object);

	g_ptr_array_unref (self->flash_descriptors);
	g_array_unref (self->checksums);

	G_OBJECT_CLASS (fu_wac_device_parent_class)->finalize (object);
}

static void
fu_wac_device_class_init (FuWacDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	object_class->finalize = fu_wac_device_finalize;
	klass_device->prepare_firmware = fu_wac_device_prepare_firmware;
	klass_device->write_firmware = fu_wac_device_write_firmware;
	klass_device->to_string = fu_wac_device_to_string;
	klass_device->setup = fu_wac_device_setup;
	klass_usb_device->open = fu_wac_device_open;
	klass_usb_device->close = fu_wac_device_close;
}
