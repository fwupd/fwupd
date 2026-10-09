/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-alc408x-common.h"
#include "fu-realtek-alc408x-device.h"
#include "fu-realtek-alc408x-firmware.h"
#include "fu-realtek-alc408x-struct.h"

#define FU_REALTEK_ALC408X_DEVICE_TIMEOUT	  5000 /* ms */
#define FU_REALTEK_ALC408X_DEVICE_HW_MODE_DELAY	  5000 /* ms */
#define FU_REALTEK_ALC408X_DEVICE_CHIP_ID	  0x1210
#define FU_REALTEK_ALC408X_DEVICE_INFO_BLOCK_SIZE 8
#define FU_REALTEK_ALC408X_DEVICE_BLOCK_SIZE	  0x1000
#define FU_REALTEK_ALC408X_DEVICE_ERASE_DONE	  0x01
#define FU_REALTEK_ALC408X_DEVICE_ERASE_RETRIES	  50
#define FU_REALTEK_ALC408X_DEVICE_ERASE_DELAY	  100 /* ms */

/* the ROM boots bank 0, and first copies bank 1 over it if the tag in bank 0 is zero */
#define FU_REALTEK_ALC408X_DEVICE_BOOT_ADDR    0x00000
#define FU_REALTEK_ALC408X_DEVICE_STAGING_ADDR 0x10000
#define FU_REALTEK_ALC408X_DEVICE_TAG_OFFSET   0x1010

/* bit 7 of both write-protect config registers is set while the flash is unprotected */
#define FU_REALTEK_ALC408X_DEVICE_WP_CONFIG_BIT 7

/* the flash status while the flash is unprotected, and once it is protected again */
#define FU_REALTEK_ALC408X_DEVICE_FLASH_STATUS_UNPROTECTED 0x00
#define FU_REALTEK_ALC408X_DEVICE_FLASH_STATUS_PROTECTED   0x8C

struct _FuRealtekAlc408xDevice {
	FuUsbDevice parent_instance;
	guint16 chip;
	guint8 hw_mode;
	gchar *build_date;
	gboolean staged; /* the boot tag is cleared, so the next reset installs bank 1 */
};

G_DEFINE_TYPE(FuRealtekAlc408xDevice, fu_realtek_alc408x_device, FU_TYPE_USB_DEVICE)

static void
fu_realtek_alc408x_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "Chip", self->chip);
	fwupd_codec_string_append_hex(str, idt, "HwMode", self->hw_mode);
	fwupd_codec_string_append(str, idt, "BuildDate", self->build_date);
}

static gboolean
fu_realtek_alc408x_device_read(FuRealtekAlc408xDevice *self,
			       FuRealtekAlc408xRequest request,
			       guint16 value,
			       guint16 idx,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	gsize actual_length = 0;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    request,
					    value,
					    idx,
					    buf,
					    bufsz,
					    &actual_length,
					    FU_REALTEK_ALC408X_DEVICE_TIMEOUT,
					    error))
		return FALSE;
	if (actual_length != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only read 0x%x of 0x%x bytes",
			    (guint)actual_length,
			    (guint)bufsz);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_write(FuRealtekAlc408xDevice *self,
				FuRealtekAlc408xRequest request,
				guint16 value,
				guint16 idx,
				const guint8 *buf,
				gsize bufsz,
				GError **error)
{
	gsize actual_length = 0;
	g_autofree guint8 *buf_mut = NULL;

	/* the transfer takes a mutable buffer */
	if (bufsz > 0) {
		buf_mut = fu_memdup_safe(buf, bufsz, error);
		if (buf_mut == NULL)
			return FALSE;
	}
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    request,
					    value,
					    idx,
					    buf_mut,
					    bufsz,
					    &actual_length,
					    FU_REALTEK_ALC408X_DEVICE_TIMEOUT,
					    error))
		return FALSE;
	if (actual_length != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only wrote 0x%x of 0x%x bytes",
			    (guint)actual_length,
			    (guint)bufsz);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_reg_read(FuRealtekAlc408xDevice *self,
				   FuRealtekAlc408xReg reg,
				   guint8 *buf,
				   gsize bufsz,
				   GError **error)
{
	if (!fu_realtek_alc408x_device_read(self,
					    FU_REALTEK_ALC408X_REQUEST_READ_REG,
					    0x0,
					    reg,
					    buf,
					    bufsz,
					    error)) {
		g_prefix_error(error, "failed to read register 0x%04x: ", (guint)reg);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_reg_write(FuRealtekAlc408xDevice *self,
				    FuRealtekAlc408xReg reg,
				    guint8 val,
				    GError **error)
{
	if (!fu_realtek_alc408x_device_write(self,
					     FU_REALTEK_ALC408X_REQUEST_WRITE_REG,
					     0x0,
					     reg,
					     &val,
					     sizeof(val),
					     error)) {
		g_prefix_error(error, "failed to write register 0x%04x: ", (guint)reg);
		return FALSE;
	}
	return TRUE;
}

/* the request value carries address bits 23:16 in its high byte */
static guint16
fu_realtek_alc408x_device_flash_value(guint32 addr)
{
	return (addr >> 8) & 0xFF00;
}

static gboolean
fu_realtek_alc408x_device_flash_read(FuRealtekAlc408xDevice *self,
				     guint32 addr,
				     guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	if (!fu_realtek_alc408x_device_read(self,
					    FU_REALTEK_ALC408X_REQUEST_READ_FLASH,
					    fu_realtek_alc408x_device_flash_value(addr),
					    addr & 0xFFFF,
					    buf,
					    bufsz,
					    error)) {
		g_prefix_error(error, "failed to read flash at 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_flash_write(FuRealtekAlc408xDevice *self,
				      guint32 addr,
				      const guint8 *buf,
				      gsize bufsz,
				      GError **error)
{
	if (!fu_realtek_alc408x_device_write(self,
					     FU_REALTEK_ALC408X_REQUEST_WRITE_FLASH,
					     fu_realtek_alc408x_device_flash_value(addr),
					     addr & 0xFFFF,
					     buf,
					     bufsz,
					     error)) {
		g_prefix_error(error, "failed to write flash at 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_erase_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	guint8 status = 0;

	if (!fu_realtek_alc408x_device_read(self,
					    FU_REALTEK_ALC408X_REQUEST_GET_ERASE_STATUS,
					    0x0,
					    0x0,
					    &status,
					    sizeof(status),
					    error)) {
		g_prefix_error_literal(error, "failed to get erase status: ");
		return FALSE;
	}
	if (status != FU_REALTEK_ALC408X_DEVICE_ERASE_DONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "erase not complete, status 0x%02x",
			    status);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_flash_erase(FuRealtekAlc408xDevice *self, guint32 addr, GError **error)
{
	if (!fu_realtek_alc408x_device_write(self,
					     FU_REALTEK_ALC408X_REQUEST_ERASE_FLASH,
					     fu_realtek_alc408x_device_flash_value(addr),
					     addr & 0xFFFF,
					     NULL,
					     0,
					     error)) {
		g_prefix_error(error, "failed to erase flash at 0x%x: ", addr);
		return FALSE;
	}

	/* the request returns as soon as the erase has started */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_realtek_alc408x_device_erase_status_cb,
				  FU_REALTEK_ALC408X_DEVICE_ERASE_RETRIES,
				  FU_REALTEK_ALC408X_DEVICE_ERASE_DELAY,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to erase flash at 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_ensure_chip_id(FuRealtekAlc408xDevice *self, GError **error)
{
	guint8 buf[2] = {0};
	guint16 chip_id;

	if (!fu_realtek_alc408x_device_reg_read(self,
						FU_REALTEK_ALC408X_REG_CHIP_ID,
						buf,
						sizeof(buf),
						error))
		return FALSE;
	chip_id = fu_memread_uint16(buf, G_BIG_ENDIAN);
	if (chip_id != FU_REALTEK_ALC408X_DEVICE_CHIP_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported chip ID 0x%04x",
			    chip_id);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_ensure_info(FuRealtekAlc408xDevice *self, GError **error)
{
	g_autoptr(FuStructRealtekAlc408xInfo) st_info = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* the info block can only be read 8 bytes at a time */
	for (guint i = 0;
	     i < FU_STRUCT_REALTEK_ALC408X_INFO_SIZE / FU_REALTEK_ALC408X_DEVICE_INFO_BLOCK_SIZE;
	     i++) {
		guint8 tmp[FU_REALTEK_ALC408X_DEVICE_INFO_BLOCK_SIZE] = {0};
		if (!fu_realtek_alc408x_device_read(self,
						    FU_REALTEK_ALC408X_REQUEST_READ_INFO,
						    i,
						    0x0,
						    tmp,
						    sizeof(tmp),
						    error)) {
			g_prefix_error(error, "failed to read info block %u: ", i);
			return FALSE;
		}
		g_byte_array_append(buf, tmp, sizeof(tmp));
	}
	st_info = fu_struct_realtek_alc408x_info_parse(buf->data, buf->len, 0x0, error);
	if (st_info == NULL)
		return FALSE;

	self->chip = fu_struct_realtek_alc408x_info_get_chip(st_info);
	g_free(self->build_date);
	self->build_date = fu_struct_realtek_alc408x_info_get_build_date(st_info);
	fu_device_set_version_raw(
	    FU_DEVICE(self),
	    ((guint64)fu_struct_realtek_alc408x_info_get_version_hi(st_info) << 32) |
		fu_struct_realtek_alc408x_info_get_version_lo(st_info));

	/* success */
	return TRUE;
}

/* select the hardware mode and read it back, which precedes each write-protect change */
static gboolean
fu_realtek_alc408x_device_ensure_hw_mode(FuRealtekAlc408xDevice *self, GError **error)
{
	if (!fu_realtek_alc408x_device_reg_write(self,
						 FU_REALTEK_ALC408X_REG_HW_MODE_SELECT,
						 0x29,
						 error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), FU_REALTEK_ALC408X_DEVICE_HW_MODE_DELAY);
	if (!fu_realtek_alc408x_device_reg_read(self,
						FU_REALTEK_ALC408X_REG_HW_MODE,
						&self->hw_mode,
						sizeof(self->hw_mode),
						error))
		return FALSE;
	return fu_realtek_alc408x_device_reg_write(self,
						   FU_REALTEK_ALC408X_REG_HW_MODE_SELECT,
						   0x15,
						   error);
}

/* set bit 7 of both write-protect config registers, or clear it */
static gboolean
fu_realtek_alc408x_device_set_wp_config(FuRealtekAlc408xDevice *self,
					gboolean unprotected,
					GError **error)
{
	guint8 config1 = 0;
	guint8 config2 = 0;

	if (!fu_realtek_alc408x_device_ensure_chip_id(self, error))
		return FALSE;
	if (!fu_realtek_alc408x_device_ensure_hw_mode(self, error))
		return FALSE;
	if (!fu_realtek_alc408x_device_reg_read(self,
						FU_REALTEK_ALC408X_REG_WRITE_PROTECT_CONFIG1,
						&config1,
						sizeof(config1),
						error))
		return FALSE;
	if (!fu_realtek_alc408x_device_reg_read(self,
						FU_REALTEK_ALC408X_REG_WRITE_PROTECT_CONFIG2,
						&config2,
						sizeof(config2),
						error))
		return FALSE;
	if (unprotected) {
		FU_BIT_SET(config1, FU_REALTEK_ALC408X_DEVICE_WP_CONFIG_BIT);
		FU_BIT_SET(config2, FU_REALTEK_ALC408X_DEVICE_WP_CONFIG_BIT);
	} else {
		FU_BIT_CLEAR(config1, FU_REALTEK_ALC408X_DEVICE_WP_CONFIG_BIT);
		FU_BIT_CLEAR(config2, FU_REALTEK_ALC408X_DEVICE_WP_CONFIG_BIT);
	}
	if (!fu_realtek_alc408x_device_reg_write(self,
						 FU_REALTEK_ALC408X_REG_WRITE_PROTECT_CONFIG1,
						 config1,
						 error))
		return FALSE;
	return fu_realtek_alc408x_device_reg_write(self,
						   FU_REALTEK_ALC408X_REG_WRITE_PROTECT_CONFIG2,
						   config2,
						   error);
}

static gboolean
fu_realtek_alc408x_device_wp_config_set_cb(FuDevice *device, GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	return fu_realtek_alc408x_device_set_wp_config(self, TRUE, error);
}

static gboolean
fu_realtek_alc408x_device_wp_config_clear_cb(FuDevice *device, GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	return fu_realtek_alc408x_device_set_wp_config(self, FALSE, error);
}

/* write the flash status, and check the flash accepted it */
static gboolean
fu_realtek_alc408x_device_write_flash_status(FuRealtekAlc408xDevice *self,
					     guint8 status,
					     GError **error)
{
	guint8 status_new = 0;

	if (!fu_realtek_alc408x_device_write(self,
					     FU_REALTEK_ALC408X_REQUEST_WRITE_FLASH_STATUS,
					     0x0,
					     0x0,
					     &status,
					     sizeof(status),
					     error)) {
		g_prefix_error_literal(error, "failed to write flash status: ");
		return FALSE;
	}
	if (!fu_realtek_alc408x_device_read(self,
					    FU_REALTEK_ALC408X_REQUEST_READ_FLASH_STATUS,
					    0x0,
					    0x0,
					    &status_new,
					    sizeof(status_new),
					    error)) {
		g_prefix_error_literal(error, "failed to read flash status: ");
		return FALSE;
	}
	if (status_new != status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "flash status is 0x%02x, expected 0x%02x",
			    status_new,
			    status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_setup(FuDevice *device, GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_realtek_alc408x_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_realtek_alc408x_device_ensure_chip_id(self, error))
		return FALSE;
	if (!fu_realtek_alc408x_device_ensure_info(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_realtek_alc408x_device_prepare_firmware(FuDevice *device,
					   FuInputStream *stream,
					   FuProgress *progress,
					   FuFirmwareParseFlags flags,
					   GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_realtek_alc408x_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID) == 0 &&
	    fu_realtek_alc408x_firmware_get_chip(FU_REALTEK_ALC408X_FIRMWARE(firmware)) !=
		self->chip) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "firmware is for chip 0x%04x, but device is 0x%04x",
		    fu_realtek_alc408x_firmware_get_chip(FU_REALTEK_ALC408X_FIRMWARE(firmware)),
		    self->chip);
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_realtek_alc408x_device_write_chunks(FuRealtekAlc408xDevice *self,
				       FuChunkArray *chunks,
				       FuProgress *progress,
				       GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_realtek_alc408x_device_flash_write(self,
							   fu_chunk_get_address(chk),
							   fu_chunk_get_data(chk),
							   fu_chunk_get_data_sz(chk),
							   error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_verify_chunks(FuRealtekAlc408xDevice *self,
					FuChunkArray *chunks,
					FuProgress *progress,
					GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autofree guint8 *buf = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		buf = g_malloc0(fu_chunk_get_data_sz(chk));
		if (!fu_realtek_alc408x_device_flash_read(self,
							  fu_chunk_get_address(chk),
							  buf,
							  fu_chunk_get_data_sz(chk),
							  error))
			return FALSE;
		if (!fu_memcmp_safe(buf,
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error)) {
			g_prefix_error(error,
				       "failed to verify flash at 0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	const guint8 tag[4] = {0x0};
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 45, "unprotect");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 3, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 45, "protect");

	/* the image is staged in bank 1 */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_REALTEK_ALC408X_DEVICE_STAGING_ADDR,
						FU_CHUNK_PAGESZ_NONE,
						FU_REALTEK_ALC408X_DEVICE_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	/* bit 7 is cleared again if anything below fails */
	locker = fu_device_locker_new_full(device,
					   fu_realtek_alc408x_device_wp_config_set_cb,
					   fu_realtek_alc408x_device_wp_config_clear_cb,
					   error);
	if (locker == NULL)
		return FALSE;
	if (!fu_realtek_alc408x_device_write_flash_status(
		self,
		FU_REALTEK_ALC408X_DEVICE_FLASH_STATUS_UNPROTECTED,
		error)) {
		g_prefix_error_literal(error, "failed to disable write protection: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* bank 0 is not touched, so the device still boots the old image if this fails */
	if (!fu_realtek_alc408x_device_flash_erase(self,
						   FU_REALTEK_ALC408X_DEVICE_STAGING_ADDR,
						   error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_realtek_alc408x_device_write_chunks(self,
						    chunks,
						    fu_progress_get_child(progress),
						    error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_realtek_alc408x_device_verify_chunks(self,
						     chunks,
						     fu_progress_get_child(progress),
						     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* only now mark bank 0 as stale, so the ROM copies bank 1 over it on the next reset */
	if (!fu_realtek_alc408x_device_flash_write(self,
						   FU_REALTEK_ALC408X_DEVICE_BOOT_ADDR +
						       FU_REALTEK_ALC408X_DEVICE_TAG_OFFSET,
						   tag,
						   sizeof(tag),
						   error)) {
		g_prefix_error_literal(error, "failed to clear boot tag: ");
		return FALSE;
	}

	/* protect the flash again */
	if (!fu_device_locker_close(locker, error))
		return FALSE;
	if (!fu_realtek_alc408x_device_write_flash_status(
		self,
		FU_REALTEK_ALC408X_DEVICE_FLASH_STATUS_PROTECTED,
		error)) {
		g_prefix_error_literal(error, "failed to enable write protection: ");
		return FALSE;
	}
	self->staged = TRUE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_alc408x_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* nothing to install, e.g. the image was only read back to verify it */
	if (!self->staged)
		return TRUE;

	/* the ROM installs the staged image, then the device re-enumerates */
	if (!fu_realtek_alc408x_device_write(self,
					     FU_REALTEK_ALC408X_REQUEST_RESET_TO_ROM,
					     0x0,
					     0x0,
					     NULL,
					     0,
					     &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) &&
		    !g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to reset: ");
			return FALSE;
		}
		g_debug("ignoring as the device is resetting: %s", error_local->message);
	}
	self->staged = FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static GBytes *
fu_realtek_alc408x_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(device);
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* the image the device booted */
	chunks = fu_chunk_array_new_virtual(FU_REALTEK_ALC408X_FIRMWARE_SIZE,
					    FU_REALTEK_ALC408X_DEVICE_BOOT_ADDR,
					    FU_CHUNK_PAGESZ_NONE,
					    FU_REALTEK_ALC408X_DEVICE_BLOCK_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autofree guint8 *tmp = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return NULL;
		tmp = g_malloc0(fu_chunk_get_data_sz(chk));
		if (!fu_realtek_alc408x_device_flash_read(self,
							  fu_chunk_get_address(chk),
							  tmp,
							  fu_chunk_get_data_sz(chk),
							  error))
			return NULL;
		g_byte_array_append(buf, tmp, fu_chunk_get_data_sz(chk));
		fu_progress_step_done(progress);
	}
	return g_bytes_new(buf->data, buf->len);
}

static gchar *
fu_realtek_alc408x_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_realtek_alc408x_version_to_string(version_raw);
}

static void
fu_realtek_alc408x_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 75, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 25, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_realtek_alc408x_device_init(FuRealtekAlc408xDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_firmware_size(FU_DEVICE(self), FU_REALTEK_ALC408X_FIRMWARE_SIZE);
	fu_device_set_install_duration(FU_DEVICE(self), 15);
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.alc408x");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_AUDIO_CARD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
}

static void
fu_realtek_alc408x_device_finalize(GObject *object)
{
	FuRealtekAlc408xDevice *self = FU_REALTEK_ALC408X_DEVICE(object);
	g_free(self->build_date);
	G_OBJECT_CLASS(fu_realtek_alc408x_device_parent_class)->finalize(object);
}

static void
fu_realtek_alc408x_device_class_init(FuRealtekAlc408xDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_realtek_alc408x_device_finalize;
	device_class->to_string = fu_realtek_alc408x_device_to_string;
	device_class->setup = fu_realtek_alc408x_device_setup;
	device_class->prepare_firmware = fu_realtek_alc408x_device_prepare_firmware;
	device_class->write_firmware = fu_realtek_alc408x_device_write_firmware;
	device_class->attach = fu_realtek_alc408x_device_attach;
	device_class->dump_firmware = fu_realtek_alc408x_device_dump_firmware;
	device_class->convert_version = fu_realtek_alc408x_device_convert_version;
	device_class->set_progress = fu_realtek_alc408x_device_set_progress;
}
