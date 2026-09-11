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
#include "fu-realtek-billboard-struct.h"

struct _FuRealtekBillboardDevice {
	FuUsbDevice parent_instance;
	guint16 user_start_bank;
	guint8 user_fw_size;
	guint32 user_flag_addr;
};

G_DEFINE_TYPE(FuRealtekBillboardDevice, fu_realtek_billboard_device, FU_TYPE_USB_DEVICE)

/**
 * fu_realtek_billboard_device_send:
 * @self: a #FuRealtekBillboardDevice
 * @request: a bRequest code, e.g. %FU_REALTEK_BILLBOARD_RQT_WRITE_FLASH
 * @value: a wValue field for the control transfer
 * @index: an wIndex field for the control transfer
 * @buf: (array length=bufsz): buffer to send
 * @bufsz: size of @buf
 * @error: (nullable): optional return location for an error
 *
 * Sends data to the device using a vendor control transfer.
 *
 * Returns: %TRUE on success
 **/
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

/**
 * fu_realtek_billboard_device_recv:
 * @self: a #FuRealtekBillboardDevice
 * @request: a bRequest code, e.g. %FU_REALTEK_BILLBOARD_RQT_READ_FLASH
 * @value: a wValue field for the control transfer
 * @index: an wIndex field for the control transfer
 * @buf: (out) (array length=bufsz): buffer to receive data
 * @bufsz: size of @buf
 * @error: (nullable): optional return location for an error
 *
 * Receives data from the device using a vendor control transfer.
 *
 * Returns: %TRUE on success
 **/
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

/**
 * fu_realtek_billboard_device_read_reg_ex:
 * @self: a #FuRealtekBillboardDevice
 * @reg: a register address
 * @val: (out): the register value read
 * @error: (nullable): optional return location for an error
 *
 * Reads an MCU register using a vendor control transfer.
 *
 * Returns: %TRUE on success
 **/
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

/**
 * fu_realtek_billboard_device_setup:
 * @device: a #FuDevice
 * @error: (nullable): optional return location for an error
 *
 * Sets up the device, reading the firmware version.
 *
 * Returns: %TRUE on success
 **/
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

	/* the flash geometry is fixed for all VID/PID, so it is not a quirk */
	fu_device_set_firmware_size_min(device, 0x1000);
	fu_device_set_firmware_size_max(device, 0x100000);

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

/**
 * fu_realtek_billboard_device_attach:
 * @device: a #FuDevice
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Re-attaches the device on the USB bus by toggling the attach bit.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_realtek_billboard_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	guint8 val = 0;
	guint16 addr = FU_REALTEK_BILLBOARD_MCU_REG_ADDR(FU_REALTEK_BILLBOARD_MCU_REG_USB);
	g_autoptr(GError) error_local = NULL;

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
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

/**
 * fu_realtek_billboard_device_write_reg_ex:
 * @self: a #FuRealtekBillboardDevice
 * @reg: a register address
 * @val: the register value to write
 * @error: (nullable): optional return location for an error
 *
 * Writes an MCU register using a vendor control transfer.
 *
 * Returns: %TRUE on success
 **/
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

/**
 * fu_realtek_billboard_device_sector_erase:
 * @self: a #FuRealtekBillboardDevice
 * @bank_id: a bank id
 * @sector_id: a sector id within the bank
 * @error: (nullable): optional return location for an error
 *
 * Erases a single flash sector using a vendor control transfer.
 *
 * Returns: %TRUE on success
 **/
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

/**
 * fu_realtek_billboard_device_bank_erase:
 * @self: a #FuRealtekBillboardDevice
 * @bank_id: a bank id
 * @error: (nullable): optional return location for an error
 *
 * Erases a whole 64 KB flash bank using a vendor control transfer.
 *
 * Returns: %TRUE on success
 **/
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

/**
 * fu_realtek_billboard_device_addr_to_value_index:
 * @addr: a 32-bit flash address
 * @wvalue: (out): the wValue field for the control transfer
 * @windex: (out): the wIndex field for the control transfer
 *
 * Splits a 32-bit flash address into the wValue/wIndex halves expected
 * by the device, per the vendor protocol:
 *   wValue = addr[31:16] (big-endian byte order within the 16-bit value)
 *   wIndex = addr[15:0]  (big-endian byte order within the 16-bit value)
 **/
static void
fu_realtek_billboard_device_addr_to_value_index(guint32 addr, guint16 *wvalue, guint16 *windex)
{
	guint8 buf[4] = {0x0};

	g_return_if_fail(wvalue != NULL);
	g_return_if_fail(windex != NULL);

	fu_memwrite_uint32(buf, addr, G_BIG_ENDIAN);
	*wvalue = fu_memread_uint16(buf, G_BIG_ENDIAN);
	*windex = fu_memread_uint16(buf + 2, G_BIG_ENDIAN);
}

/**
 * fu_realtek_billboard_device_read_flash_data:
 * @self: a #FuRealtekBillboardDevice
 * @flash_addr: a 32-bit flash address
 * @len: number of bytes to read
 * @buf: (out caller-allocates) (array length=len): buffer for the data
 * @error: (nullable): optional return location for an error
 *
 * Reads flash data from the device in 256 byte packets.
 *
 * Returns: %TRUE on success
 **/
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
		guint16 wvalue;
		guint16 windex;

		fu_realtek_billboard_device_addr_to_value_index(addr, &wvalue, &windex);
		if (!fu_realtek_billboard_device_recv(
			self,
			FU_REALTEK_BILLBOARD_RQT_READ_FLASH,
			wvalue,
			windex,
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to read flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}
	if ((len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE) != 0) {
		guint32 addr = flash_addr + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE);
		guint16 wvalue;
		guint16 windex;

		fu_realtek_billboard_device_addr_to_value_index(addr, &wvalue, &windex);
		if (!fu_realtek_billboard_device_recv(
			self,
			FU_REALTEK_BILLBOARD_RQT_READ_FLASH,
			wvalue,
			windex,
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to read flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * fu_realtek_billboard_device_write_flash_data:
 * @self: a #FuRealtekBillboardDevice
 * @flash_addr: a 32-bit flash address
 * @len: number of bytes to write
 * @buf: (array length=len): data to write
 * @error: (nullable): optional return location for an error
 *
 * Writes flash data to the device in 256 byte packets, setting the ISP flash
 * port access method first.
 *
 * Returns: %TRUE on success
 **/
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
		guint16 wvalue;
		guint16 windex;

		fu_realtek_billboard_device_addr_to_value_index(addr, &wvalue, &windex);
		if (!fu_realtek_billboard_device_send(
			self,
			FU_REALTEK_BILLBOARD_RQT_WRITE_FLASH,
			wvalue,
			windex,
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to write flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}
	if ((len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE) != 0) {
		guint32 addr = flash_addr + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE);
		guint16 wvalue;
		guint16 windex;

		fu_realtek_billboard_device_addr_to_value_index(addr, &wvalue, &windex);
		if (!fu_realtek_billboard_device_send(
			self,
			FU_REALTEK_BILLBOARD_RQT_WRITE_FLASH,
			wvalue,
			windex,
			buf + (i * FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE),
			len % FU_REALTEK_BILLBOARD_MAX_PACKET_SIZE,
			error)) {
			g_prefix_error(error, "failed to write flash at 0x%x: ", (guint)addr);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * fu_realtek_billboard_device_clear_user_flag:
 * @self: a #FuRealtekBillboardDevice
 * @error: (nullable): optional return location for an error
 *
 * Clears the user flag bytes in flash, so the device knows an update is in
 * progress.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_realtek_billboard_device_clear_user_flag(FuRealtekBillboardDevice *self, GError **error)
{
	guint32 sector_base;
	guint16 sector_offset;
	g_autofree guint8 *sector_buf = g_malloc0(FU_REALTEK_BILLBOARD_SECTOR_SIZE);

	/* cache the full sector that contains user_flag_addr */
	sector_base = self->user_flag_addr & ~(FU_REALTEK_BILLBOARD_SECTOR_SIZE - 1);
	sector_offset = (guint16)(self->user_flag_addr - sector_base);
	/* the flag must fit inside a single sector, otherwise the memset
	 * below writes past the end of sector_buf */
	if (sector_offset > FU_REALTEK_BILLBOARD_SECTOR_SIZE - FU_REALTEK_BILLBOARD_FLAG_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "user flag address 0x%x not within a single sector",
			    self->user_flag_addr);
		return FALSE;
	}
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

/**
 * fu_realtek_billboard_device_write_user_flag:
 * @self: a #FuRealtekBillboardDevice
 * @error: (nullable): optional return location for an error
 *
 * Writes the user flag bytes in flash to mark the update as complete.
 *
 * Returns: %TRUE on success
 **/
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

/**
 * fu_realtek_billboard_device_write_firmware_to_flash:
 * @self: a #FuRealtekBillboardDevice
 * @stream: an input stream with the firmware image
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Writes the firmware image to flash, one 64 KB bank at a time, verifying
 * each bank after writing.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_realtek_billboard_device_write_firmware_to_flash(FuRealtekBillboardDevice *self,
						    FuInputStream *stream,
						    FuProgress *progress,
						    GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	/* split the stream into one chunk per 64 KB bank */
	chunks = fu_chunk_array_new_from_stream(stream,
						(gsize)self->user_start_bank << 16,
						FU_CHUNK_PAGESZ_NONE,
						FU_REALTEK_BILLBOARD_BANK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autofree guint8 *verify_buf = NULL;
		const guint8 *blob_data;
		guint32 bank_base;
		guint32 bank_len;
		guint16 bank_id;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		bank_base = fu_chunk_get_address(chk);
		bank_len = fu_chunk_get_data_sz(chk);
		bank_id = (guint16)(bank_base >> 16);
		blob_data = fu_chunk_get_data(chk);
		verify_buf = g_malloc(bank_len);

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
		fu_progress_step_done(progress);
	}
	return TRUE;
}

/**
 * fu_realtek_billboard_device_query_dual_bank:
 * @self: a #FuRealtekBillboardDevice
 * @error: (nullable): optional return location for an error
 *
 * Queries the user start bank and user flag address from the device.
 *
 * Returns: %TRUE on success
 **/
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
	/* the user firmware must fit inside the bank id space, otherwise
	 * bank_id++ wraps to channel zero during the firmware write */
	if ((guint32)self->user_start_bank + self->user_fw_size > G_MAXUINT16 + 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "dual bank range 0x%x..0x%x exceeds bank id space",
			    self->user_start_bank,
			    (guint16)(self->user_start_bank + self->user_fw_size));
		return FALSE;
	}
	/* big endian: buf[1] MSB, buf[4] LSB */
	self->user_flag_addr = fu_memread_uint32(buf + 1, G_BIG_ENDIAN);

	return TRUE;
}

/**
 * fu_realtek_billboard_device_disable_isp:
 * @self: a #FuRealtekBillboardDevice
 * @error: (nullable): optional return location for an error
 *
 * Disables ISP mode and lets the firmware enable flash protection.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_realtek_billboard_device_disable_isp(FuRealtekBillboardDevice *self, GError **error)
{
	/* isp disable, fw enable flash protect */
	if (!fu_realtek_billboard_device_send(self,
					      FU_REALTEK_BILLBOARD_RQT_ISP_ENABLE,
					      0,
					      0,
					      NULL,
					      0,
					      error))
		return FALSE;
	return TRUE;
}

/**
 * fu_realtek_billboard_device_write_firmware:
 * @device: a #FuDevice
 * @firmware: a #FuFirmware
 * @progress: a #FuProgress
 * @flags: install flags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Writes new firmware to the device using the dual-bank update flow.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_realtek_billboard_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuRealtekBillboardDevice *self = FU_REALTEK_BILLBOARD_DEVICE(device);
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GError) error_local = NULL;
	gsize streamsz = 0;
	guint32 expected_sz;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, NULL);

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

	/* get binary stream */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL) {
		g_prefix_error_literal(error, "failed to get firmware stream: ");
		return FALSE;
	}

	/* check firmware size matches expected user bank size */
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
	if (!fu_realtek_billboard_device_clear_user_flag(self, &error_local)) {
		g_prefix_error_literal(&error_local, "clear user flag failed: ");
		/* best-effort isp disable so stamped-flash writes do not
		 * leave the MCU with flash protect off */
		if (!fu_realtek_billboard_device_disable_isp(self, NULL))
			g_warning("failed to disable ISP mode after write failure");
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write firmware to flash via 256-byte chunks */
	if (!fu_realtek_billboard_device_write_firmware_to_flash(self,
								 stream,
								 fu_progress_get_child(progress),
								 &error_local)) {
		g_prefix_error_literal(&error_local, "firmware flash write failed: ");
		/* best-effort isp disable so stamped-flash writes do not
		 * leave the MCU with flash protect off */
		if (!fu_realtek_billboard_device_disable_isp(self, NULL))
			g_warning("failed to disable ISP mode after write failure");
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write user flag to mark update complete */
	if (!fu_realtek_billboard_device_write_user_flag(self, &error_local)) {
		g_prefix_error_literal(&error_local, "write user flag failed: ");
		/* best-effort isp disable so stamped-flash writes do not
		 * leave the MCU with flash protect off */
		if (!fu_realtek_billboard_device_disable_isp(self, NULL))
			g_warning("failed to disable ISP mode after write failure");
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* isp disable, fw enable flash protect */
	if (!fu_realtek_billboard_device_disable_isp(self, &error_local)) {
		g_prefix_error_literal(&error_local, "failed to disable ISP mode: ");
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_realtek_billboard_device_set_progress:
 * @device: a #FuDevice
 * @progress: a #FuProgress
 *
 * Sets the progress steps for the device update flow.
 **/
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

/**
 * fu_realtek_billboard_device_init:
 * @self: a #FuRealtekBillboardDevice
 *
 * Initializes the device instance.
 **/
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

/**
 * fu_realtek_billboard_device_class_init:
 * @klass: a #FuRealtekBillboardDeviceClass
 *
 * Initializes the device class, registering the vfuncs.
 **/
static void
fu_realtek_billboard_device_class_init(FuRealtekBillboardDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_realtek_billboard_device_setup;
	device_class->write_firmware = fu_realtek_billboard_device_write_firmware;
	device_class->attach = fu_realtek_billboard_device_attach;
	device_class->set_progress = fu_realtek_billboard_device_set_progress;
}
