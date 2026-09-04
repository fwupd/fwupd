/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-realtek-billboard-common.h"
#include "fu-realtek-billboard-device.h"

struct _FuRealtekBillboardDevice {
	FuUsbDevice parent_instance;
	guint16 user_start_bank;
	guint8 user_fw_size;
	guint32 user_flag_addr;
};

G_DEFINE_TYPE(FuRealtekBillboardDevice, fu_realtek_billboard_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_realtek_billboard_device_send(FuRealtekBillboardDevice *self,
				 guint8 request,
				 guint16 value,
				 guint16 index,
				 const guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	gsize actual_len = 0;
	g_autofree guint8 *buf_tmp = NULL;

	g_return_val_if_fail(buf != NULL || bufsz == 0, FALSE);

	if (bufsz > 0) {
		buf_tmp = fu_memdup_safe(buf, bufsz, error);
		if (buf_tmp == NULL)
			return FALSE;
	}
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    request,
					    value,
					    index,
					    buf_tmp,
					    bufsz,
					    &actual_len,
					    FU_REALTEK_BILLBOARD_TRANSACTION_TIMEOUT,
					    error)) {
		g_prefix_error_literal(error, "send error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "send length mismatch");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_recv(FuRealtekBillboardDevice *self,
				 guint8 request,
				 guint16 value,
				 guint16 index,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	gsize actual_len = 0;

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz > 0, FALSE);

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    request,
					    value,
					    index,
					    buf,
					    bufsz,
					    &actual_len,
					    FU_REALTEK_BILLBOARD_TRANSACTION_TIMEOUT,
					    error)) {
		g_prefix_error_literal(error, "recv error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "recv length mismatch");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_read_reg_ex(FuRealtekBillboardDevice *self,
					guint16 reg,
					guint8 *val,
					GError **error)
{
	return fu_realtek_billboard_device_recv(self,
						FU_REALTEK_BILLBOARD_RQT_GET_REGISTER,
						reg,
						0,
						val,
						sizeof(*val),
						error);
}

static gboolean
fu_realtek_billboard_device_setup(FuDevice *device, GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	guint8 fw_version = 0;
	guint8 fw_sub_version = 0;
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_realtek_billboard_device_parent_class)->setup(device, error))
		return FALSE;

	/* read firmware version */
	if (!fu_realtek_billboard_device_read_reg_ex(self,
						     FU_REALTEK_BILLBOARD_REG_FW_VERSION,
						     &fw_version,
						     error)) {
		g_prefix_error_literal(error, "failed to read FW version (reg 0x0004): ");
		return FALSE;
	}

	fw_version &= 0x3F;

	if (!fu_realtek_billboard_device_read_reg_ex(self,
						     FU_REALTEK_BILLBOARD_REG_FW_SUB_VERSION,
						     &fw_sub_version,
						     error)) {
		g_prefix_error_literal(error, "failed to read FW sub version (reg 0x0007): ");
		return FALSE;
	}

	/* the device only reports two version bytes, use a fixed quad */
	version = g_strdup_printf("%u.%u.0.0", fw_version, fw_sub_version);
	fu_device_set_version(device, version);
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	guint8 val = 0;
	guint16 addr = FU_REALTEK_BILLBOARD_MCU_REG_ADDR(FU_REALTEK_BILLBOARD_MCU_REG_USB);

	/* read current MCU register value */
	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_RQT_GET_REGISTER,
					      addr,
					      0,
					      &val,
					      sizeof(val),
					      error)) {
		g_prefix_error(error,
			       "failed to read MCU reg 0x%02x: ",
			       (guint)FU_REALTEK_BILLBOARD_MCU_REG_USB);
		return FALSE;
	}

	/* clear the attach bit, then set it again to re-attach on the bus */
	val &= ~FU_REALTEK_BILLBOARD_MCU_REG_USB_ATTACH;
	if (!fu_realtek_billboard_device_send(self,
					      FU_REALTEK_BILLBOARD_RQT_SET_REGISTER,
					      addr,
					      0,
					      &val,
					      sizeof(val),
					      error)) {
		g_prefix_error(error,
			       "failed to clear MCU reg 0x%02x: ",
			       (guint)FU_REALTEK_BILLBOARD_MCU_REG_USB);
		return FALSE;
	}
	val |= FU_REALTEK_BILLBOARD_MCU_REG_USB_ATTACH;

	/* this causes the device to reset and re-enumerate immediately, so
	 * the control transfer may not complete cleanly -- ignore any error */
	{
		g_autoptr(GError) error_local = NULL;
		if (!fu_realtek_billboard_device_send(self,
						      FU_REALTEK_BILLBOARD_RQT_SET_REGISTER,
						      addr,
						      0,
						      &val,
						      sizeof(val),
						      &error_local)) {
			g_debug("ignoring error writing MCU reg 0x%02x for device reset: %s",
				(guint)FU_REALTEK_BILLBOARD_MCU_REG_USB,
				error_local->message);
		}
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_write_reg_ex(FuRealtekBillboardDevice *self,
					 guint16 reg,
					 guint8 val,
					 GError **error)
{
	return fu_realtek_billboard_device_send(self,
						FU_REALTEK_BILLBOARD_RQT_SET_REGISTER,
						reg,
						0,
						&val,
						sizeof(val),
						error);
}

static gboolean
fu_realtek_billboard_device_sector_erase(FuRealtekBillboardDevice *self,
					 guint16 bank_id,
					 guint8 sector_id,
					 GError **error)
{
	return fu_realtek_billboard_device_send(self,
						FU_REALTEK_BILLBOARD_RQT_SECTOR_ERASE,
						bank_id,
						(guint16)sector_id,
						NULL,
						0,
						error);
}

static gboolean
fu_realtek_billboard_device_bank_erase(FuRealtekBillboardDevice *self,
				       guint16 bank_id,
				       GError **error)
{
	return fu_realtek_billboard_device_send(self,
						FU_REALTEK_BILLBOARD_RQT_BANK_ERASE,
						bank_id,
						0,
						NULL,
						0,
						error);
}

static gboolean
fu_realtek_billboard_device_read_flash_data(FuRealtekBillboardDevice *self,
					    guint32 flash_addr,
					    guint32 len,
					    guint8 *buf,
					    GError **error)
{
	guint32 i;

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(len > 0, FALSE);

	for (i = 0; i < len / FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE; i++) {
		guint32 addr = flash_addr + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE);
		if (!fu_realtek_billboard_device_recv(
			self,
			FU_REALTEK_BILLBOARD_RQT_READ_FLASH,
			(guint16)((addr >> 16) & 0xFFFF),
			(guint16)(addr & 0xFFFF),
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to read flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}
	if ((len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE) != 0) {
		guint32 addr = flash_addr + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE);
		if (!fu_realtek_billboard_device_recv(
			self,
			FU_REALTEK_BILLBOARD_RQT_READ_FLASH,
			(guint16)((addr >> 16) & 0xFFFF),
			(guint16)(addr & 0xFFFF),
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to read flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_realtek_billboard_device_write_flash_data(FuRealtekBillboardDevice *self,
					     guint32 flash_addr,
					     guint32 len,
					     const guint8 *buf,
					     GError **error)
{
	guint32 i;

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(len > 0, FALSE);

	/* the device needs the FW flash port access method opcode set to ISP
	 * before any flash data is written (firmware payload and user flag) */
	if (!fu_realtek_billboard_device_write_reg_ex(
		self,
		FU_REALTEK_BILLBOARD_MCU_REG_ADDR(FU_REALTEK_BILLBOARD_MCU_REG_FW_FLASH_PORT_ACC),
		FU_REALTEK_BILLBOARD_FW_FLASH_PORT_ACC_ISP,
		error)) {
		g_prefix_error_literal(error, "failed to set flash port access method: ");
		return FALSE;
	}

	for (i = 0; i < len / FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE; i++) {
		guint32 addr = flash_addr + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE);
		if (!fu_realtek_billboard_device_send(
			self,
			FU_REALTEK_BILLBOARD_RQT_WRITE_FLASH,
			(guint16)((addr >> 16) & 0xFFFF),
			(guint16)(addr & 0xFFFF),
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to write flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}
	if ((len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE) != 0) {
		guint32 addr = flash_addr + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE);
		if (!fu_realtek_billboard_device_send(
			self,
			FU_REALTEK_BILLBOARD_RQT_WRITE_FLASH,
			(guint16)((addr >> 16) & 0xFFFF),
			(guint16)(addr & 0xFFFF),
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to write flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_realtek_billboard_device_clear_user_flag(FuRealtekBillboardDevice *self, GError **error)
{
	guint32 sector_base;
	guint16 sector_offset;
	g_autofree guint8 *sector_buf = g_malloc0(FU_REALTEK_BILLBOARD_SECTOR_SIZE);

	/* cache the full sector that contains user_flag_addr */
	sector_base = self->user_flag_addr & ~(FU_REALTEK_BILLBOARD_SECTOR_SIZE - 1);
	sector_offset = (guint16)(self->user_flag_addr - sector_base);
	if (!fu_realtek_billboard_device_read_flash_data(self,
							 sector_base,
							 FU_REALTEK_BILLBOARD_SECTOR_SIZE,
							 sector_buf,
							 error)) {
		g_prefix_error(error, "failed to read sector at 0x%x: ", (guint)sector_base);
		return FALSE;
	}

	/* set the 5 flag bytes to 0xFF */
	memset(sector_buf + sector_offset, 0xFF, FU_REALTEK_BILLBOARD_FLAG_SIZE);

	/* erase the sector */
	if (!fu_realtek_billboard_device_sector_erase(self,
						      (guint16)(sector_base >> 16),
						      (guint8)((sector_base >> 12) & 0x0F),
						      error)) {
		g_prefix_error(error, "failed to erase sector at 0x%x: ", (guint)sector_base);
		return FALSE;
	}

	/* write the modified sector back */
	if (!fu_realtek_billboard_device_write_flash_data(self,
							  sector_base,
							  FU_REALTEK_BILLBOARD_SECTOR_SIZE,
							  sector_buf,
							  error)) {
		g_prefix_error(error, "failed to write sector at 0x%x: ", (guint)sector_base);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_realtek_billboard_device_write_user_flag(FuRealtekBillboardDevice *self, GError **error)
{
	const guint8 flag_data[FU_REALTEK_BILLBOARD_FLAG_SIZE] = {0xAA, 0xAA, 0xAA, 0xFF, 0xFF};

	/* write 0xAA,0xAA,0xAA,0xFF,0xFF at user flag address */
	if (!fu_realtek_billboard_device_write_flash_data(self,
							  self->user_flag_addr,
							  FU_REALTEK_BILLBOARD_FLAG_SIZE,
							  flag_data,
							  error)) {
		g_prefix_error(error,
			       "failed to write user flag at 0x%x: ",
			       (guint)self->user_flag_addr);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_realtek_billboard_device_write_firmware_to_flash(FuRealtekBillboardDevice *self,
						    FuInputStream *stream,
						    FuProgress *progress,
						    GError **error)
{
	guint16 bank_id = self->user_start_bank;
	guint32 remain = (guint32)self->user_fw_size << 16;
	guint32 total_banks =
	    (remain + FU_REALTEK_BILLBOARD_BANK_SIZE - 1) / FU_REALTEK_BILLBOARD_BANK_SIZE;
	guint32 offset = 0;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, total_banks);
	while (remain > 0) {
		guint32 bank_base = (guint32)bank_id << 16;
		guint32 bank_len = MIN(remain, FU_REALTEK_BILLBOARD_BANK_SIZE);
		g_autofree guint8 *verify_buf = g_malloc(bank_len);
		g_autoptr(GBytes) blob = NULL;
		const guint8 *blob_data;

		blob = fu_input_stream_read_bytes(stream, offset, bank_len, NULL, error);
		if (blob == NULL)
			return FALSE;
		blob_data = g_bytes_get_data(blob, NULL);

		/* erase current bank before writing */
		if (!fu_realtek_billboard_device_bank_erase(self, bank_id, error)) {
			g_prefix_error(error, "failed to erase bank %u: ", (guint)bank_id);
			return FALSE;
		}

		/* write the whole bank in a single request */
		if (!fu_realtek_billboard_device_write_flash_data(self,
								  bank_base,
								  bank_len,
								  blob_data,
								  error)) {
			g_prefix_error(error,
				       "failed to write bank %u at 0x%x: ",
				       (guint)bank_id,
				       (guint)bank_base);
			return FALSE;
		}

		/* read the bank back and confirm it was written correctly */
		if (!fu_realtek_billboard_device_read_flash_data(self,
								 bank_base,
								 bank_len,
								 verify_buf,
								 error)) {
			g_prefix_error(error,
				       "failed to read back bank %u at 0x%x for verification: ",
				       (guint)bank_id,
				       (guint)bank_base);
			return FALSE;
		}
		if (memcmp(blob_data, verify_buf, bank_len) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware verification failed for bank %u at 0x%x",
				    (guint)bank_id,
				    (guint)bank_base);
			return FALSE;
		}
		remain -= bank_len;
		offset += bank_len;
		bank_id++;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_realtek_billboard_device_query_dual_bank(FuRealtekBillboardDevice *self, GError **error)
{
	guint8 buf[5] = {0};

	/* ask for user start bank */
	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_RQT_DUAL_BANK,
					      FU_REALTEK_BILLBOARD_DUAL_BANK_OP_GET_START_ADDR,
					      0,
					      buf,
					      sizeof(buf),
					      error)) {
		g_prefix_error_literal(error, "failed to get user start bank: ");
		return FALSE;
	}
	if (buf[0] != FU_REALTEK_BILLBOARD_RQT_DUAL_BANK) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unexpected echo in dual bank response");
		return FALSE;
	}
	/* little endian: buf[1] low byte, buf[2] high byte */
	self->user_start_bank = fu_memread_uint16(buf + 1, G_LITTLE_ENDIAN);
	self->user_fw_size = buf[3];
	/* ask for user flag address */
	if (!fu_realtek_billboard_device_recv(self,
					      FU_REALTEK_BILLBOARD_RQT_DUAL_BANK,
					      FU_REALTEK_BILLBOARD_DUAL_BANK_OP_GET_FLAG_ADDR,
					      0,
					      buf,
					      sizeof(buf),
					      error)) {
		g_prefix_error_literal(error, "failed to get user flag address: ");
		return FALSE;
	}
	if (buf[0] != FU_REALTEK_BILLBOARD_RQT_DUAL_BANK) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unexpected echo in dual bank response");
		return FALSE;
	}
	/* big endian: buf[1] MSB, buf[4] LSB */
	self->user_flag_addr = fu_memread_uint32(buf + 1, G_BIG_ENDIAN);

	return TRUE;
}

static gboolean
fu_realtek_billboard_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	g_autoptr(FuInputStream) stream = NULL;

	/* query dual bank info before writing */
	if (!fu_realtek_billboard_device_query_dual_bank(self, error)) {
		g_prefix_error_literal(error, "dual bank query failed: ");
		return FALSE;
	}
	if (self->user_fw_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "device returned zero firmware size");
		return FALSE;
	}

	/* isp enable, only after the dual bank query has completed */
	if (!fu_realtek_billboard_device_send(self,
					      FU_REALTEK_BILLBOARD_RQT_ISP_ENABLE,
					      1,
					      0,
					      NULL,
					      0,
					      error)) {
		g_prefix_error_literal(error, "failed to enable ISP mode: ");
		return FALSE;
	}

	/* the device does not re-enumerate on entering ISP mode, but needs
	 * time to switch execution context before it responds to flash
	 * commands again */
	fu_device_sleep_full(device, 100, progress);

	/* clear user flag before firmware write */
	if (!fu_realtek_billboard_device_clear_user_flag(self, error)) {
		g_prefix_error_literal(error, "clear user flag failed: ");
		return FALSE;
	}

	/* get binary stream */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL) {
		g_prefix_error_literal(error, "failed to get firmware stream: ");
		return FALSE;
	}

	/* check firmware size matches expected user bank size */
	{
		gsize streamsz = 0;
		guint32 expected_sz;

		if (!fu_input_stream_size(stream, &streamsz, error))
			return FALSE;
		expected_sz = (guint32)self->user_fw_size << 16;
		if ((guint32)streamsz != expected_sz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware size %u does not match expected %u (%u banks)",
				    (guint)streamsz,
				    expected_sz,
				    (guint)self->user_fw_size);
			return FALSE;
		}
	}

	/* write firmware to flash via 256-byte chunks */
	if (!fu_realtek_billboard_device_write_firmware_to_flash(self, stream, progress, error)) {
		g_prefix_error_literal(error, "firmware flash write failed: ");
		return FALSE;
	}

	/* write user flag to mark update complete */
	if (!fu_realtek_billboard_device_write_user_flag(self, error)) {
		g_prefix_error_literal(error, "write user flag failed: ");
		return FALSE;
	}

	/* isp disable, fw enable flash protect */
	if (!fu_realtek_billboard_device_send(self,
					      FU_REALTEK_BILLBOARD_RQT_ISP_ENABLE,
					      0,
					      0,
					      NULL,
					      0,
					      error)) {
		g_prefix_error_literal(error, "failed to enable ISP mode: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_realtek_billboard_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_realtek_billboard_device_init(FuRealtekBillboardDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.billboard");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	/* the USB Billboard interface is always #0 on this device */
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x0);
}

static void
fu_realtek_billboard_device_class_init(FuRealtekBillboardDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_realtek_billboard_device_setup;
	device_class->write_firmware = fu_realtek_billboard_device_write_firmware;
	device_class->attach = fu_realtek_billboard_device_attach;
	device_class->set_progress = fu_realtek_billboard_device_set_progress;
}
