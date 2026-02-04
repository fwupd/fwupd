/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include <string.h>

#include "fu-novatek-ts-firmware.h"
#include "fu-novatek-ts-plugin.h"

struct _FuNovatekTsFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuNovatekTsFirmware, fu_novatek_ts_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_novatek_ts_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	blob = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
	if (blob == NULL)
		return FALSE;
	fu_firmware_set_bytes(firmware, blob);
	return TRUE;
}

static void
fu_novatek_ts_firmware_init(FuNovatekTsFirmware *self)
{
}

static void
fu_novatek_ts_firmware_class_init(FuNovatekTsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_novatek_ts_firmware_parse;
}

FuFirmware *
fu_novatek_ts_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_NOVATEK_TS_FIRMWARE, NULL));
}

static gboolean
fu_novatek_ts_firmware_check_end_flag(const guint8 *bin_data, guint32 *bin_size, GError **error)
{
	const guint8 *base = bin_data;
	const guint32 sz = *bin_size;
	if (sz < FW_BIN_END_FLAG_LEN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware blob too small for end flag");
		return FALSE;
	}

	if (memcmp(base + (sz - FW_BIN_END_FLAG_LEN), FW_BIN_END_FLAG_STR, FW_BIN_END_FLAG_LEN) !=
	    0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "binary end flag not found at end (expected [%s])",
			    FW_BIN_END_FLAG_STR);
		return FALSE;
	}

	g_info("found hid fw bin flag [%s] at end of firmware", FW_BIN_END_FLAG_STR);
	return TRUE;
}

gboolean
fu_novatek_ts_firmware_prepare_bin(FuNovatekTsFirmware *self,
				   guint8 **bin_data,
				   guint32 *bin_size,
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
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid firmware blob (data=%p size=0x%zX)",
			    data,
			    size);
		return FALSE;
	}

	if (size > MAX_BIN_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware blob too large (0x%zX > 0x%X)",
			    size,
			    (guint32)MAX_BIN_SIZE);
		return FALSE;
	}

	*bin_data = g_malloc(size);
	if (*bin_data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "malloc for firmware blob failed (size=0x%zX)",
			    size);
		return FALSE;
	}
	if (!fu_memcpy_safe(*bin_data, size, 0, data, size, 0, size, error)) {
		g_prefix_error_literal(error, "copying firmware blob failed: ");
		return FALSE;
	}
	*bin_size = (guint32)size;

	/* check and trim according to end-flag if needed */
	if (!fu_novatek_ts_firmware_check_end_flag(*bin_data, bin_size, error))
		return FALSE;

	if (flash_start_addr == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "normal FW flash should not start from 0");
		return FALSE;
	}

	/* always use FLASH_NORMAL start (0x2000) */
	if (flash_start_addr < FLASH_SECTOR_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "flash start addr too low: 0x%X",
			    flash_start_addr);
		return FALSE;
	}

	/* drop leading header region so data starts at flash_start_addr */
	if (flash_start_addr > *bin_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware blob too small (size=0x%X, start=0x%X)",
			    *bin_size,
			    flash_start_addr);
		return FALSE;
	}
	if (flash_start_addr > 0) {
		memmove(*bin_data, *bin_data + flash_start_addr, *bin_size - flash_start_addr);
		*bin_size -= flash_start_addr;
	}

	g_info("flashing starts from 0x%X", flash_start_addr);
	g_info("size of bin for update = 0x%05X", *bin_size);
	g_info("flash range to write = 0x%X-0x%X",
	       flash_start_addr,
	       flash_start_addr + *bin_size - 1);

	if (flash_max_size > 0 && *bin_size > flash_max_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "flash size 0x%X exceeds max 0x%X",
			    *bin_size,
			    flash_max_size);
		return FALSE;
	}
	if (flash_max_size > 0) {
		guint32 flash_end = flash_start_addr + *bin_size;
		guint32 flash_limit = flash_start_addr + flash_max_size;
		if (flash_end > flash_limit) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "flash end 0x%X exceeds limit 0x%X",
				    flash_end - 1,
				    flash_limit - 1);
			return FALSE;
		}
	}

	return TRUE;
}
