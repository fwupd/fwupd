/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include <string.h>

#include "fu-nvt-ts-firmware.h"
#include "fu-nvt-ts-plugin.h"

struct _FuNvtTsFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuNvtTsFirmware, fu_nvt_ts_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_nvt_ts_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	(void)flags;

	blob = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
	if (blob == NULL)
		return FALSE;
	fu_firmware_set_bytes(firmware, blob);
	return TRUE;
}

static void
fu_nvt_ts_firmware_init(FuNvtTsFirmware *self)
{
}

static void
fu_nvt_ts_firmware_class_init(FuNvtTsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_nvt_ts_firmware_parse;
}

FuFirmware *
fu_nvt_ts_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_NVT_TS_FIRMWARE, NULL));
}

void
fu_nvt_ts_firmware_bin_clear(FuNvtTsFwBin *fwb)
{
	if (fwb->bin_data != NULL) {
		g_free(fwb->bin_data);
		fwb->bin_data = NULL;
	}
	fwb->bin_size = 0;
	fwb->flash_start_addr = 0;
}

static gboolean
fu_nvt_ts_firmware_find_fw_bin_end_flag(const guint8 *base,
					guint32 size,
					guint32 *flag_offset,
					guint32 *delta_out,
					GError **error)
{
	const guint32 step = 0x1000;
	guint32 delta = 0;
	guint32 offset = 0;
	guint8 end_char[FW_BIN_END_FLAG_LEN] = {0};

	for (delta = 0; delta <= 0x1000; delta += step) {
		if (size < FW_BIN_END_FLAG_LEN + delta)
			return FALSE;

		offset = size - FW_BIN_END_FLAG_LEN - delta;
		if (!fu_memcpy_safe(end_char,
				    sizeof(end_char),
				    0,
				    base,
				    size,
				    offset,
				    FW_BIN_END_FLAG_LEN,
				    error)) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "copying end flag failed: ");
			return FALSE;
		}
		if (memcmp(end_char, FW_BIN_END_FLAG_STR, FW_BIN_END_FLAG_LEN) == 0) {
			if (flag_offset != NULL)
				*flag_offset = offset;
			if (delta_out != NULL)
				*delta_out = delta;
			return TRUE;
		}

		if (size < FW_BIN_END_FLAG_LEN + delta + step)
			break;
	}

	return FALSE;
}

static gboolean
fu_nvt_ts_firmware_check_end_flag(FuNvtTsFwBin *fwb, GError **error)
{
	guint32 flag_offset = 0;
	guint32 delta = 0;
	const guint8 *base = fwb->bin_data;
	const guint32 sz = fwb->bin_size;
	guint8 end_char[FW_BIN_END_FLAG_LEN + 1] = {0};

	if (!fu_nvt_ts_firmware_find_fw_bin_end_flag(base, sz, &flag_offset, &delta, error)) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "binary end flag not found at end or at (-0x1000) steps "
				    "(expected [%s]), abort.",
				    FW_BIN_END_FLAG_STR);
		return FALSE;
	}

	if (!fu_memcpy_safe((guint8 *)end_char,
			    sizeof(end_char),
			    0,
			    base,
			    sz,
			    flag_offset,
			    FW_BIN_END_FLAG_LEN,
			    error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "copying end flag failed: ");
		return FALSE;
	}

	g_info("found hid fw bin flag [%s] at offset 0x%X (probe delta 0x%X)",
	       FW_BIN_END_FLAG_STR,
	       flag_offset,
	       delta);
	g_info("raw end bytes = [%s]", end_char);

	/* clamp size to include the full "NVT" trailer */
	fwb->bin_size = flag_offset + FW_BIN_END_FLAG_LEN;
	return TRUE;
}

gboolean
fu_nvt_ts_firmware_prepare_fwb(FuNvtTsFirmware *self,
			       FuNvtTsFwBin *fwb,
			       guint32 flash_start_addr,
			       guint32 flash_max_size,
			       GError **error)
{
	gsize size = 0;
	const guint8 *data = NULL;
	FuFirmware *firmware = FU_FIRMWARE(self);
	g_autoptr(GBytes) blob = NULL;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	data = g_bytes_get_data(blob, &size);
	if (data == NULL || size == 0) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "invalid firmware blob (data=%p size=0x%zX)",
				    data,
				    size);
		return FALSE;
	}

	fu_nvt_ts_firmware_bin_clear(fwb);

	if (size > MAX_BIN_SIZE) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "firmware blob too large (0x%zX > 0x%X)",
				    size,
				    (guint32)MAX_BIN_SIZE);
		return FALSE;
	}

	fwb->bin_data = g_malloc(size);
	if (fwb->bin_data == NULL) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "malloc for firmware blob failed (size=0x%zX)",
				    size);
		return FALSE;
	}
	if (!fu_memcpy_safe(fwb->bin_data, size, 0, data, size, 0, size, error)) {
		SET_ERROR_OR_PREFIX(error, FWUPD_ERROR_INTERNAL, "copying firmware blob failed: ");
		return FALSE;
	}
	fwb->bin_size = (guint32)size;

	/* check and trim according to end-flag if needed */
	if (!fu_nvt_ts_firmware_check_end_flag(fwb, error))
		return FALSE;

	if (flash_start_addr == 0) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "normal FW flash should not start from 0");
		return FALSE;
	}

	/* always use FLASH_NORMAL start (0x2000) */
	fwb->flash_start_addr = flash_start_addr;
	if (fwb->flash_start_addr < FLASH_SECTOR_SIZE) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "flash start addr too low: 0x%X",
				    fwb->flash_start_addr);
		return FALSE;
	}

	/* drop leading header region so data starts at flash_start_addr */
	if (fwb->flash_start_addr > fwb->bin_size) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "firmware blob too small (size=0x%X, start=0x%X)",
				    fwb->bin_size,
				    fwb->flash_start_addr);
		return FALSE;
	}
	if (fwb->flash_start_addr > 0) {
		memmove(fwb->bin_data,
			fwb->bin_data + fwb->flash_start_addr,
			fwb->bin_size - fwb->flash_start_addr);
		fwb->bin_size -= fwb->flash_start_addr;
	}

	g_info("flashing starts from 0x%X", fwb->flash_start_addr);
	g_info("size of bin for update = 0x%05X", fwb->bin_size);
	g_info("flash range to write = 0x%X-0x%X",
	       fwb->flash_start_addr,
	       fwb->flash_start_addr + fwb->bin_size - 1);

	if (flash_max_size > 0 && fwb->bin_size > flash_max_size) {
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INTERNAL,
				    "flash size 0x%X exceeds max 0x%X",
				    fwb->bin_size,
				    flash_max_size);
		SET_ERROR_OR_PREFIX(error,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware image exceeds max flash size");
		return FALSE;
	}
	if (flash_max_size > 0) {
		guint32 flash_end = fwb->flash_start_addr + fwb->bin_size;
		guint32 flash_limit = flash_start_addr + flash_max_size;
		if (flash_end > flash_limit) {
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INTERNAL,
					    "flash end 0x%X exceeds limit 0x%X",
					    flash_end - 1,
					    flash_limit - 1);
			SET_ERROR_OR_PREFIX(error,
					    FWUPD_ERROR_INVALID_FILE,
					    "firmware image exceeds flash limit");
			return FALSE;
		}
	}

	return TRUE;
}
