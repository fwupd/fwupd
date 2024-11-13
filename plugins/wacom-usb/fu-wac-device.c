/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-device.h"
#include "fu-wac-firmware.h"
#include "fu-wac-module-bluetooth-id6.h"
#include "fu-wac-module-bluetooth-id9.h"
#include "fu-wac-module-bluetooth.h"
#include "fu-wac-module-scaler.h"
#include "fu-wac-module-sub-cpu.h"
#include "fu-wac-module-touch-id7.h"
#include "fu-wac-module-touch.h"
#include "fu-wac-struct.h"

typedef struct {
	guint32 start_addr;
	guint32 block_sz;
	guint16 write_sz; /* bit 15 is write protection flag */
} FuWacFlashDescriptor;

#define FU_WAC_DEVICE_TIMEOUT		 5000 /* ms */
#define FU_WAC_DEVICE_MODULE_RETRY_DELAY 100  /* ms */

struct _FuWacDevice {
	FuHidDevice parent_instance;
	GPtrArray *flash_descriptors;
	GArray *checksums;
	guint32 status_word;
	guint16 firmware_index;
	guint16 loader_ver;
	guint16 read_data_sz;
	guint16 write_word_sz;
	guint16 write_block_sz; /* usb transfer size */
	guint16 nr_flash_blocks;
	guint16 configuration;
};

G_DEFINE_TYPE(FuWacDevice, fu_wac_device, FU_TYPE_HID_DEVICE)

static gboolean
fu_wac_device_flash_descriptor_is_wp(const FuWacFlashDescriptor *fd)
{
	return fd->write_sz & 0x8000;
}

static void
fu_wac_device_flash_descriptor_to_string(FuWacFlashDescriptor *fd, guint idt, GString *str)
{
	fwupd_codec_string_append_hex(str, idt, "StartAddr", fd->start_addr);
	fwupd_codec_string_append_hex(str, idt, "BlockSize", fd->block_sz);
	fwupd_codec_string_append_hex(str, idt, "WriteSize", fd->write_sz & ~0x8000);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "Protected",
				       fu_wac_device_flash_descriptor_is_wp(fd));
}

static void
fu_wac_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuWacDevice *self = FU_WAC_DEVICE(device);
	g_autofree gchar *status_str = NULL;

	if (self->firmware_index != 0xffff) {
		g_autofree gchar *tmp = g_strdup_printf("0x%04x", self->firmware_index);
		fwupd_codec_string_append(str, idt, "FwIndex", tmp);
	}
	if (self->loader_ver > 0) {
		g_autofree gchar *tmp = g_strdup_printf("0x%04x", (guint)self->loader_ver);
		fwupd_codec_string_append(str, idt, "LoaderVer", tmp);
	}
	if (self->read_data_sz > 0) {
		g_autofree gchar *tmp = g_strdup_printf("0x%04x", (guint)self->read_data_sz);
		fwupd_codec_string_append(str, idt, "ReadDataSize", tmp);
	}
	if (self->write_word_sz > 0) {
		g_autofree gchar *tmp = g_strdup_printf("0x%04x", (guint)self->write_word_sz);
		fwupd_codec_string_append(str, idt, "WriteWordSize", tmp);
	}
	if (self->write_block_sz > 0) {
		g_autofree gchar *tmp = g_strdup_printf("0x%04x", (guint)self->write_block_sz);
		fwupd_codec_string_append(str, idt, "WriteBlockSize", tmp);
	}
	if (self->nr_flash_blocks > 0) {
		g_autofree gchar *tmp = g_strdup_printf("0x%04x", (guint)self->nr_flash_blocks);
		fwupd_codec_string_append(str, idt, "NrFlashBlocks", tmp);
	}
	if (self->configuration != 0xffff) {
		g_autofree gchar *tmp = g_strdup_printf("0x%04x", (guint)self->configuration);
		fwupd_codec_string_append(str, idt, "Configuration", tmp);
	}
	for (guint i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index(self->flash_descriptors, i);
		g_autofree gchar *title = g_strdup_printf("FlashDescriptor%02u", i);
		fwupd_codec_string_append(str, idt, title, "");
		fu_wac_device_flash_descriptor_to_string(fd, idt + 1, str);
	}
	status_str = fu_wac_device_status_to_string(self->status_word);
	fwupd_codec_string_append(str, idt, "Status", status_str);
}

gboolean
fu_wac_device_get_feature_report(FuWacDevice *self,
				 guint8 *buf,
				 gsize bufsz,
				 FuHidDeviceFlags flags,
				 GError **error)
{
	guint8 cmd = buf[0];

	/* hit hardware */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      cmd,
				      buf,
				      bufsz,
				      FU_WAC_DEVICE_TIMEOUT,
				      flags | FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;

	/* check packet */
	if (buf[0] != cmd) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command response was %i expected %i",
			    buf[0],
			    cmd);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_wac_device_set_feature_report(FuWacDevice *self,
				 guint8 *buf,
				 gsize bufsz,
				 FuHidDeviceFlags flags,
				 GError **error)
{
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					buf[0],
					buf,
					bufsz,
					FU_WAC_DEVICE_TIMEOUT,
					flags | FU_HID_DEVICE_FLAG_IS_FEATURE |
					    FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

static gboolean
fu_wac_device_ensure_flash_descriptors(FuWacDevice *self, GError **error)
{
	gsize sz = (self->nr_flash_blocks * 10) + 1;
	g_autofree guint8 *buf = NULL;

	/* already done */
	if (self->flash_descriptors->len > 0)
		return TRUE;

	/* hit hardware */
	buf = g_malloc(sz);
	memset(buf, 0xff, sz);
	buf[0] = FU_WAC_REPORT_ID_GET_FLASH_DESCRIPTOR;
	if (!fu_wac_device_get_feature_report(self, buf, sz, FU_HID_DEVICE_FLAG_NONE, error))
		return FALSE;

	/* parse */
	for (guint i = 0; i < self->nr_flash_blocks; i++) {
		g_autofree FuWacFlashDescriptor *fd = g_new0(FuWacFlashDescriptor, 1);
		const guint blksz = 0x0A;
		if (!fu_memread_uint32_safe(buf,
					    sz,
					    (i * blksz) + 1,
					    &fd->start_addr,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint32_safe(buf,
					    sz,
					    (i * blksz) + 5,
					    &fd->block_sz,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint16_safe(buf,
					    sz,
					    (i * blksz) + 9,
					    &fd->write_sz,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		g_ptr_array_add(self->flash_descriptors, g_steal_pointer(&fd));
	}
	if (self->flash_descriptors->len > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "too many flash descriptors for hardware: 0x%x",
			    self->flash_descriptors->len);
		return FALSE;
	}

	g_info("added %u flash descriptors", self->flash_descriptors->len);
	return TRUE;
}

static gboolean
fu_wac_device_ensure_status(FuWacDevice *self, GError **error)
{
	g_autofree gchar *str = NULL;
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_GET_STATUS, [1 ... 4] = 0xff};

	/* hit hardware */
	if (!fu_wac_device_get_feature_report(self,
					      buf,
					      sizeof(buf),
					      FU_HID_DEVICE_FLAG_NONE,
					      error))
		return FALSE;

	/* parse */
	self->status_word = fu_memread_uint32(buf + 1, G_LITTLE_ENDIAN);
	str = fu_wac_device_status_to_string(self->status_word);
	g_debug("status now: %s", str);
	return TRUE;
}

static gboolean
fu_wac_device_ensure_checksums(FuWacDevice *self, GError **error)
{
	gsize sz = (self->nr_flash_blocks * 4) + 5;
	guint32 updater_version;
	g_autofree guint8 *buf = g_malloc(sz);

	/* hit hardware */
	memset(buf, 0xff, sz);
	buf[0] = FU_WAC_REPORT_ID_GET_CHECKSUMS;
	if (!fu_wac_device_get_feature_report(self, buf, sz, FU_HID_DEVICE_FLAG_NONE, error))
		return FALSE;

	/* parse */
	updater_version = fu_memread_uint32(buf + 1, G_LITTLE_ENDIAN);
	g_info("updater-version: %" G_GUINT32_FORMAT, updater_version);

	/* get block checksums */
	g_array_set_size(self->checksums, 0);
	for (guint i = 0; i < self->nr_flash_blocks; i++) {
		guint32 csum = fu_memread_uint32(buf + 5 + (i * 4), G_LITTLE_ENDIAN);
		g_debug("checksum block %02u: 0x%08x", i, (guint)csum);
		g_array_append_val(self->checksums, csum);
	}
	g_debug("added %u checksums", self->flash_descriptors->len);

	return TRUE;
}

static gboolean
fu_wac_device_ensure_firmware_index(FuWacDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_GET_CURRENT_FIRMWARE_IDX, [1 ... 2] = 0xff};

	/* hit hardware */
	if (!fu_wac_device_get_feature_report(self,
					      buf,
					      sizeof(buf),
					      FU_HID_DEVICE_FLAG_NONE,
					      error))
		return FALSE;

	/* parse */
	self->firmware_index = fu_memread_uint16(buf + 1, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_wac_device_ensure_parameters(FuWacDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_GET_PARAMETERS, [1 ... 12] = 0xff};

	/* hit hardware */
	if (!fu_wac_device_get_feature_report(self,
					      buf,
					      sizeof(buf),
					      FU_HID_DEVICE_FLAG_NONE,
					      error))
		return FALSE;

	/* parse */
	self->loader_ver = fu_memread_uint16(buf + 1, G_LITTLE_ENDIAN);
	self->read_data_sz = fu_memread_uint16(buf + 3, G_LITTLE_ENDIAN);
	self->write_word_sz = fu_memread_uint16(buf + 5, G_LITTLE_ENDIAN);
	self->write_block_sz = fu_memread_uint16(buf + 7, G_LITTLE_ENDIAN);
	self->nr_flash_blocks = fu_memread_uint16(buf + 9, G_LITTLE_ENDIAN);
	self->configuration = fu_memread_uint16(buf + 11, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_wac_device_write_block(FuWacDevice *self, guint32 addr, GBytes *blob, GError **error)
{
	const guint8 *tmp;
	gsize bufsz = self->write_block_sz + 5;
	gsize sz = 0;
	g_autofree guint8 *buf = NULL;

	/* check size */
	tmp = g_bytes_get_data(blob, &sz);
	if (sz > self->write_block_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "packet was too large at %" G_GSIZE_FORMAT " bytes",
			    sz);
		return FALSE;
	}

	/* build packet */
	buf = g_malloc(bufsz);
	memset(buf, 0xff, bufsz);
	buf[0] = FU_WAC_REPORT_ID_WRITE_BLOCK;
	fu_memwrite_uint32(buf + 1, addr, G_LITTLE_ENDIAN);
	if (sz > 0) {
		if (!fu_memcpy_safe(buf,
				    bufsz,
				    0x5, /* dst */
				    tmp,
				    sz,
				    0x0, /* src */
				    sz,
				    error))
			return FALSE;
	}

	/* hit hardware */
	return fu_wac_device_set_feature_report(self, buf, bufsz, FU_HID_DEVICE_FLAG_NONE, error);
}

static gboolean
fu_wac_device_erase_block(FuWacDevice *self, guint32 addr, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_ERASE_BLOCK, [1 ... 4] = 0xff};

	/* build packet */
	fu_memwrite_uint32(buf + 1, addr, G_LITTLE_ENDIAN);

	/* hit hardware */
	return fu_wac_device_set_feature_report(self,
						buf,
						sizeof(buf),
						FU_HID_DEVICE_FLAG_NONE,
						error);
}

gboolean
fu_wac_device_update_reset(FuWacDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_UPDATE_RESET, [1 ... 4] = 0xff};

	/* hit hardware */
	return fu_wac_device_set_feature_report(self,
						buf,
						sizeof(buf),
						FU_HID_DEVICE_FLAG_NONE,
						error);
}

static gboolean
fu_wac_device_set_checksum_of_block(FuWacDevice *self,
				    guint16 block_nr,
				    guint32 checksum,
				    GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_SET_CHECKSUM_FOR_BLOCK, [1 ... 6] = 0xff};

	/* build packet */
	fu_memwrite_uint16(buf + 1, block_nr, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf + 3, checksum, G_LITTLE_ENDIAN);

	/* hit hardware */
	return fu_wac_device_set_feature_report(self,
						buf,
						sizeof(buf),
						FU_HID_DEVICE_FLAG_NONE,
						error);
}

static gboolean
fu_wac_device_calculate_checksum_of_block(FuWacDevice *self, guint16 block_nr, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_CALCULATE_CHECKSUM_FOR_BLOCK, [1 ... 2] = 0xff};

	/* build packet */
	fu_memwrite_uint16(buf + 1, block_nr, G_LITTLE_ENDIAN);

	/* hit hardware */
	return fu_wac_device_set_feature_report(self,
						buf,
						sizeof(buf),
						FU_HID_DEVICE_FLAG_NONE,
						error);
}

static gboolean
fu_wac_device_write_checksum_table(FuWacDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_WRITE_CHECKSUM_TABLE, [1 ... 4] = 0xff};

	/* hit hardware */
	return fu_wac_device_set_feature_report(self,
						buf,
						sizeof(buf),
						FU_HID_DEVICE_FLAG_NONE,
						error);
}

gboolean
fu_wac_device_switch_to_flash_loader(FuWacDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_SWITCH_TO_FLASH_LOADER, [1] = 0x05, [2] = 0x6a};

	/* hit hardware */
	return fu_wac_device_set_feature_report(self,
						buf,
						sizeof(buf),
						FU_HID_DEVICE_FLAG_NONE,
						error);
}

static gboolean
fu_wac_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuWacDevice *self = FU_WAC_DEVICE(device);
	gsize blocks_done = 0;
	gsize blocks_total = 0;
	g_autofree guint32 *csum_local = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GHashTable) fd_blobs = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	/* get current selected device */
	if (!fu_wac_device_ensure_firmware_index(self, error))
		return FALSE;

	/* use the correct image from the firmware */
	img = fu_firmware_get_image_by_idx(firmware, self->firmware_index == 1 ? 1 : 0, error);
	if (img == NULL)
		return FALSE;
	g_debug("using image at addr 0x%0x", (guint)fu_firmware_get_addr(img));

	/* get firmware parameters (page sz and transfer sz) */
	if (!fu_wac_device_ensure_parameters(self, error))
		return FALSE;

	/* get the current flash descriptors */
	if (!fu_wac_device_ensure_flash_descriptors(self, error))
		return FALSE;

	/* get the updater protocol version */
	if (!fu_wac_device_ensure_checksums(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* clear all checksums of pages */
	for (guint i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index(self->flash_descriptors, i);
		if (fu_wac_device_flash_descriptor_is_wp(fd))
			continue;
		if (!fu_wac_device_set_checksum_of_block(self, i, 0x0, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* get the blobs for each chunk */
	fd_blobs = g_hash_table_new_full(g_direct_hash,
					 g_direct_equal,
					 NULL,
					 (GDestroyNotify)g_bytes_unref);
	for (guint i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index(self->flash_descriptors, i);
		GBytes *blob_block;
		g_autoptr(GBytes) blob_tmp = NULL;

		if (fu_wac_device_flash_descriptor_is_wp(fd))
			continue;
		blob_tmp = fu_firmware_write_chunk(img, fd->start_addr, fd->block_sz, NULL);
		if (blob_tmp == NULL)
			break;
		blob_block = fu_bytes_pad(blob_tmp, fd->block_sz);
		g_hash_table_insert(fd_blobs, fd, blob_block);
	}

	/* checksum actions post-write */
	blocks_total = g_hash_table_size(fd_blobs);

	/* write the data into the flash page */
	csum_local = g_new0(guint32, self->flash_descriptors->len);
	for (guint i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index(self->flash_descriptors, i);
		GBytes *blob_block;
		g_autoptr(FuChunkArray) chunks = NULL;

		/* if page is protected */
		if (fu_wac_device_flash_descriptor_is_wp(fd))
			continue;

		/* get data for page */
		blob_block = g_hash_table_lookup(fd_blobs, fd);
		if (blob_block == NULL)
			break;

		/* ignore empty blocks */
		if (fu_bytes_is_empty(blob_block)) {
			g_debug("empty block, ignoring");
			fu_progress_set_percentage_full(fu_progress_get_child(progress),
							blocks_done++,
							blocks_total);
			continue;
		}

		/* erase entire block */
		if (!fu_wac_device_erase_block(self, i, error))
			return FALSE;

		/* write block in chunks */
		chunks = fu_chunk_array_new_from_bytes(blob_block,
						       fd->start_addr,
						       FU_CHUNK_PAGESZ_NONE,
						       self->write_block_sz);
		for (guint j = 0; j < fu_chunk_array_length(chunks); j++) {
			g_autoptr(FuChunk) chk = NULL;
			g_autoptr(GBytes) blob_chunk = NULL;

			/* prepare chunk */
			chk = fu_chunk_array_index(chunks, j, error);
			if (chk == NULL)
				return FALSE;
			blob_chunk = fu_chunk_get_bytes(chk);
			if (!fu_wac_device_write_block(self,
						       fu_chunk_get_address(chk),
						       blob_chunk,
						       error))
				return FALSE;
		}

		/* calculate expected checksum and save to device RAM */
		csum_local[i] = GUINT32_TO_LE(fu_sum32w_bytes(blob_block, G_LITTLE_ENDIAN));
		g_debug("block checksum %02u: 0x%08x", i, csum_local[i]);
		if (!fu_wac_device_set_checksum_of_block(self, i, csum_local[i], error))
			return FALSE;

		/* update device progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						blocks_done++,
						blocks_total);
	}
	fu_progress_step_done(progress);

	/* check at least one block was written */
	if (blocks_done == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "empty firmware image or all blocks write-protected");
		return FALSE;
	}

	/* calculate CRC inside device */
	for (guint i = 0; i < self->flash_descriptors->len; i++) {
		if (!fu_wac_device_calculate_checksum_of_block(self, i, error))
			return FALSE;
	}

	/* read all CRC of all pages and verify with local CRC */
	if (!fu_wac_device_ensure_checksums(self, error))
		return FALSE;
	for (guint i = 0; i < self->flash_descriptors->len; i++) {
		FuWacFlashDescriptor *fd = g_ptr_array_index(self->flash_descriptors, i);
		GBytes *blob_block;
		guint32 csum_rom;

		/* if page is protected */
		if (fu_wac_device_flash_descriptor_is_wp(fd))
			continue;

		/* no more written pages */
		blob_block = g_hash_table_lookup(fd_blobs, fd);
		if (blob_block == NULL)
			continue;
		if (fu_bytes_is_empty(blob_block))
			continue;

		/* check checksum matches */
		csum_rom = g_array_index(self->checksums, guint32, i);
		if (csum_rom != csum_local[i]) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed local checksum at block %u, "
				    "got 0x%08x expected 0x%08x",
				    i,
				    (guint)csum_rom,
				    (guint)csum_local[i]);
			return FALSE;
		}
		g_debug("matched checksum at block %u of 0x%08x", i, csum_rom);
	}
	fu_progress_step_done(progress);

	/* store host CRC into flash */
	if (!fu_wac_device_write_checksum_table(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_wac_device_add_modules_bluetooth(FuWacDevice *self, GError **error)
{
	g_autofree gchar *name = NULL;
	g_autofree gchar *name_id6 = NULL;
	g_autoptr(FuWacModule) module = NULL;
	g_autoptr(FuWacModule) module_id6 = NULL;
	guint16 fw_ver;

	/* it can take up to 5s to get the new version after a fw update */
	for (guint i = 0; i < 5; i++) {
		guint8 buf[] = {[0] = FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_BLUETOOTH,
				[1 ... 14] = 0xff};
		if (!fu_wac_device_get_feature_report(self,
						      buf,
						      sizeof(buf),
						      FU_HID_DEVICE_FLAG_NONE,
						      error)) {
			g_prefix_error(error, "Failed to get GetFirmwareVersionBluetooth: ");
			return FALSE;
		}
		if (!fu_memread_uint16_safe(buf, sizeof(buf), 1, &fw_ver, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (fw_ver != 0)
			break;
		fu_device_sleep(FU_DEVICE(self), 1000); /* ms */
	}

	/* Success! But legacy bluetooth can't tell us which module the device needs.
	 * Initialize both and rely on the firmware update containing the appropriate
	 * package.
	 */
	name = g_strdup_printf("%s [Legacy Bluetooth Module]", fu_device_get_name(FU_DEVICE(self)));
	module = fu_wac_module_bluetooth_new(FU_DEVICE(self));
	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
	fu_device_set_name(FU_DEVICE(module), name);
	fu_device_set_version_raw(FU_DEVICE(module), fw_ver);

	name_id6 = g_strdup_printf("%s [Legacy Bluetooth Module (ID6)]",
				   fu_device_get_name(FU_DEVICE(self)));
	module_id6 = fu_wac_module_bluetooth_id6_new(FU_DEVICE(self));
	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module_id6));
	fu_device_set_name(FU_DEVICE(module_id6), name_id6);
	fu_device_set_version_raw(FU_DEVICE(module_id6), fw_ver);
	return TRUE;
}

static gboolean
fu_wac_device_add_modules_legacy(FuWacDevice *self, GError **error)
{
	g_autoptr(GError) error_bt = NULL;

	/* optional bluetooth */
	if (!fu_wac_device_add_modules_bluetooth(self, &error_bt))
		g_debug("no bluetooth hardware: %s", error_bt->message);

	return TRUE;
}

static gboolean
fu_wac_device_add_modules_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint8 buf[] = {[0] = FU_WAC_REPORT_ID_FW_DESCRIPTOR, [1 ... 31] = 0xff};
	GByteArray *out = (GByteArray *)user_data;

	if (!fu_wac_device_get_feature_report(FU_WAC_DEVICE(device),
					      buf,
					      sizeof(buf),
					      FU_HID_DEVICE_FLAG_NONE,
					      error)) {
		g_prefix_error(error, "failed to get DeviceFirmwareDescriptor: ");
		return FALSE;
	}

	/* verify bootloader is compatible */
	if (buf[1] != 0x01) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "bootloader major version not compatible");
		return FALSE;
	}

	/* verify the number of submodules is possible */
	if (buf[3] > (512 - 4) / 4) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "number of submodules is impossible");
		return FALSE;
	}

	/* copy here, since version 0 is valid for transitional module state */
	if (!fu_memcpy_safe(out->data, out->len, 0, buf, sizeof(buf), 0, out->len, error))
		return FALSE;

	/* validate versions of each module */
	for (guint8 i = 0; i < buf[3]; i++) {
		guint8 fw_type = buf[(i * 4) + 4] & ~0x80;
		guint16 ver;

		/* check if module is in transitional state or requires re-flashing */
		if (!fu_memread_uint16_safe(buf,
					    sizeof(buf),
					    (i * 4) + 5,
					    &ver,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
		if (ver == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "module %u has error state",
				    fw_type);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_wac_device_add_modules(FuWacDevice *self, GError **error)
{
	g_autofree gchar *version_bootloader = NULL;
	guint16 boot_ver;
	guint8 number_modules;
	gsize offset = 0;
	g_autoptr(FuStructModuleDesc) st_desc = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;

	g_byte_array_set_size(buf, 32);
	/* wait for all modules started successfully */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_wac_device_add_modules_cb,
				  FU_WAC_DEVICE_MODULE_RETRY_DELAY,
				  FU_WAC_DEVICE_TIMEOUT / FU_WAC_DEVICE_MODULE_RETRY_DELAY,
				  buf,
				  &error_local)) {
		if (error_local->code != FWUPD_ERROR_INVALID_DATA) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_warning("%s", error_local->message);
	}
	fu_dump_raw(G_LOG_DOMAIN, "modules", buf->data, buf->len);

	/* bootloader version */
	st_desc = fu_struct_module_desc_parse(buf->data, buf->len, offset, error);
	if (st_desc == NULL)
		return FALSE;
	boot_ver = fu_struct_module_desc_get_bootloader_version(st_desc);
	version_bootloader = fu_version_from_uint16(boot_ver, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version_bootloader(FU_DEVICE(self), version_bootloader);
	fu_device_set_version_bootloader_raw(FU_DEVICE(self), boot_ver);

	/* no padding */
	offset += FU_STRUCT_MODULE_DESC_SIZE;

	/* get versions of each module */
	number_modules = fu_struct_module_desc_get_number_modules(st_desc);
	for (guint8 i = 0; i < number_modules; i++) {
		guint32 ver;
		guint16 ver2;
		FuWacModuleFwType fw_type;
		g_autofree gchar *name = NULL;
		g_autoptr(FuStructModuleItem) st_item = NULL;
		g_autoptr(FuWacModule) module = NULL;

		st_item = fu_struct_module_item_parse(buf->data, buf->len, offset, error);
		if (st_item == NULL)
			return FALSE;

		ver = fu_struct_module_item_get_version(st_item);
		ver2 = fu_struct_module_item_get_version2(st_item);

		/*
  		* When ver2 is available and not 0, it is appended to ver in order to make it BCD
  		* 32bits, otherwise it stays BCD 16bits.
    		*/
		if (ver2 != 0xFF && ver2 != 0) {
			ver = (ver << 16);
			ver |= (ver2 << 8);
		}

		fw_type = fu_struct_module_item_get_kind(st_item) & 0x7F;
		switch (fw_type) {
		case FU_WAC_MODULE_FW_TYPE_TOUCH:
			module = fu_wac_module_touch_new(FU_DEVICE(self));
			name = g_strdup_printf("%s [Touch Module]",
					       fu_device_get_name(FU_DEVICE(self)));
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
			fu_device_set_name(FU_DEVICE(module), name);
			fu_device_set_version_raw(FU_DEVICE(module), ver);
			break;
		case FU_WAC_MODULE_FW_TYPE_TOUCH_ID7:
			module = fu_wac_module_touch_id7_new(FU_DEVICE(self));
			name = g_strdup_printf("%s [Touch Module]",
					       fu_device_get_name(FU_DEVICE(self)));
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
			fu_device_set_name(FU_DEVICE(module), name);
			fu_device_set_summary(FU_DEVICE(module), "ID7");
			fu_device_set_version_raw(FU_DEVICE(module), ver);
			break;
		case FU_WAC_MODULE_FW_TYPE_BLUETOOTH:
			module = fu_wac_module_bluetooth_new(FU_DEVICE(self));
			name = g_strdup_printf("%s [Bluetooth Module]",
					       fu_device_get_name(FU_DEVICE(self)));
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
			fu_device_set_name(FU_DEVICE(module), name);
			fu_device_set_version_raw(FU_DEVICE(module), ver);
			break;
		case FU_WAC_MODULE_FW_TYPE_BLUETOOTH_ID6:
			module = fu_wac_module_bluetooth_id6_new(FU_DEVICE(self));
			name = g_strdup_printf("%s [Bluetooth Module]",
					       fu_device_get_name(FU_DEVICE(self)));
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
			fu_device_set_name(FU_DEVICE(module), name);
			fu_device_set_summary(FU_DEVICE(module), "ID6");
			fu_device_set_version_raw(FU_DEVICE(module), ver);
			break;
		case FU_WAC_MODULE_FW_TYPE_SCALER:
			module = fu_wac_module_scaler_new(FU_DEVICE(self));
			name = g_strdup_printf("%s [Scaler Module]",
					       fu_device_get_name(FU_DEVICE(self)));
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
			fu_device_set_name(FU_DEVICE(module), name);
			fu_device_set_version_raw(FU_DEVICE(module), ver);
			break;
		case FU_WAC_MODULE_FW_TYPE_BLUETOOTH_ID9:
			module = fu_wac_module_bluetooth_id9_new(FU_DEVICE(self));
			name = g_strdup_printf("%s [Bluetooth Module]",
					       fu_device_get_name(FU_DEVICE(self)));
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
			fu_device_set_name(FU_DEVICE(module), name);
			fu_device_set_summary(FU_DEVICE(module), "ID9");
			fu_device_set_version_raw(FU_DEVICE(module), ver);
			break;
		case FU_WAC_MODULE_FW_TYPE_SUB_CPU:
			module = fu_wac_module_sub_cpu_new(FU_DEVICE(self));
			name = g_strdup_printf("%s [Sub CPU Module]",
					       fu_device_get_name(FU_DEVICE(self)));
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(module));
			fu_device_set_name(FU_DEVICE(module), name);
			fu_device_set_version_raw(FU_DEVICE(module), ver);
			break;
		case FU_WAC_MODULE_FW_TYPE_MAIN:
			fu_device_set_version_raw(FU_DEVICE(self), ver);
			break;
		default:
			g_warning("unknown submodule type 0x%0x", fw_type);
			break;
		}
		offset += FU_STRUCT_MODULE_ITEM_SIZE;
	}
	return TRUE;
}

static gboolean
fu_wac_device_setup(FuDevice *device, GError **error)
{
	FuWacDevice *self = FU_WAC_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_wac_device_parent_class)->setup(device, error))
		return FALSE;

	/* get current status */
	if (!fu_wac_device_ensure_status(self, error))
		return FALSE;

	/* get version of each sub-module */
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION)) {
		if (!fu_wac_device_add_modules_legacy(self, error))
			return FALSE;
	} else {
		if (!fu_wac_device_add_modules(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wac_device_close(FuDevice *device, GError **error)
{
	/* reattach wacom.ko */
	if (!fu_usb_device_release_interface(FU_USB_DEVICE(device),
					     0x00, /* HID */
					     FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER,
					     error)) {
		g_prefix_error(error, "failed to re-attach interface: ");
		return FALSE;
	}

	/* The hidcore subsystem uses a generic power_supply that has a deferred
	 * work item that will lock the device. When removing the power_supply,
	 * we take the lock, then cancel the work item which needs to take the
	 * lock too. This needs to be fixed in the kernel, but for the moment
	 * this should let the kernel unstick itself. */
	fu_device_sleep(device, 20); /* ms */

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS(fu_wac_device_parent_class)->close(device, error);
}

static void
fu_wac_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_wac_device_convert_version(FuDevice *device, guint64 version_raw)
{
	if (version_raw > G_MAXUINT16)
		return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));

	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_wac_device_init(FuWacDevice *self)
{
	self->flash_descriptors = g_ptr_array_new_with_free_func(g_free);
	self->checksums = g_array_new(FALSE, FALSE, sizeof(guint32));
	self->configuration = 0xffff;
	self->firmware_index = 0xffff;
	fu_device_add_protocol(FU_DEVICE(self), "com.wacom.usb");
	fu_device_add_icon(FU_DEVICE(self), "input-tablet");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_install_duration(FU_DEVICE(self), 10);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_WAC_FIRMWARE);
	fu_device_retry_set_delay(FU_DEVICE(self), 30); /* ms */
}

static void
fu_wac_device_finalize(GObject *object)
{
	FuWacDevice *self = FU_WAC_DEVICE(object);

	g_ptr_array_unref(self->flash_descriptors);
	g_array_unref(self->checksums);

	G_OBJECT_CLASS(fu_wac_device_parent_class)->finalize(object);
}

static void
fu_wac_device_class_init(FuWacDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_wac_device_finalize;
	device_class->write_firmware = fu_wac_device_write_firmware;
	device_class->to_string = fu_wac_device_to_string;
	device_class->setup = fu_wac_device_setup;
	device_class->close = fu_wac_device_close;
	device_class->set_progress = fu_wac_device_set_progress;
	device_class->convert_version = fu_wac_device_convert_version;
}
