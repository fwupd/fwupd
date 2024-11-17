/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-usb-common.h"
#include "fu-algoltek-usb-device.h"
#include "fu-algoltek-usb-firmware.h"
#include "fu-algoltek-usb-struct.h"

struct _FuAlgoltekUsbDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbDevice, fu_algoltek_usb_device, FU_TYPE_USB_DEVICE)

#define FU_ALGOLTEK_USB_DEVICE_FLAG_ERS_SKIP_FIRST_SECTOR "ers-skip-first-sector"

static gboolean
fu_algoltek_usb_device_ctrl_transfer(FuAlgoltekUsbDevice *self,
				     FuUsbDirection direction,
				     FuAlgoltekCmd algoltek_cmd,
				     guint16 value,
				     guint16 index,
				     GByteArray *buf,
				     guint8 len,
				     GError **error)
{
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      direction,
					      FU_USB_REQUEST_TYPE_VENDOR,
					      FU_USB_RECIPIENT_INTERFACE,
					      algoltek_cmd,
					      value,
					      index,
					      buf->data,
					      len,
					      NULL,
					      ALGOLTEK_DEVICE_USB_TIMEOUT,
					      NULL,
					      error);
}

static GByteArray *
fu_algoltek_usb_device_rdr(FuAlgoltekUsbDevice *self, int address, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_address_pkt_new();

	fu_struct_algoltek_cmd_address_pkt_set_len(st, 5);
	fu_struct_algoltek_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_CMD_RDR);
	fu_struct_algoltek_cmd_address_pkt_set_address(st, address);
	fu_struct_algoltek_cmd_address_pkt_set_checksum(st, ~fu_sum8(st->data, st->len) + 1);

	if (!fu_algoltek_usb_device_ctrl_transfer(self,
						  FU_USB_DIRECTION_DEVICE_TO_HOST,
						  FU_ALGOLTEK_CMD_RDR,
						  address,
						  0xFFFF,
						  st,
						  st->len,
						  error))
		return NULL;

	/* success */
	return g_steal_pointer(&st);
}

static GByteArray *
fu_algoltek_usb_device_rdv(FuAlgoltekUsbDevice *self, GError **error)
{
	guint16 version_prefix;
	g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_transfer_pkt_new();
	g_autoptr(GByteArray) version_data = g_byte_array_new();

	fu_struct_algoltek_cmd_transfer_pkt_set_len(st, 3);
	fu_struct_algoltek_cmd_transfer_pkt_set_cmd(st, FU_ALGOLTEK_CMD_RDV);
	fu_struct_algoltek_cmd_transfer_pkt_set_checksum(st, ~fu_sum8(st->data, st->len) + 1);

	if (!fu_algoltek_usb_device_ctrl_transfer(self,
						  FU_USB_DIRECTION_DEVICE_TO_HOST,
						  FU_ALGOLTEK_CMD_RDV,
						  0xFFFF,
						  0xFFFF,
						  st,
						  st->len,
						  error))
		return NULL;

	if (!fu_memread_uint16_safe(st->data, st->len, 2, &version_prefix, G_BIG_ENDIAN, error))
		return NULL;

	if (version_prefix == 0x4147) {
		guint8 underscore_count = 0;

		/* remove len, cmd bytes and "AG" prefixes */
		for (guint32 i = 4; i < st->len; i++) {
			if (st->data[i] == '_') {
				underscore_count++;
				if (underscore_count == 1)
					continue;
			}
			if (underscore_count > 2)
				break;
			if (underscore_count > 0)
				fu_byte_array_append_uint8(version_data, st->data[i]);
		}
	} else {
		/* remove len and cmd bytes */
		for (guint32 i = 2; i < st->len; i++) {
			if (st->data[i] < 128)
				fu_byte_array_append_uint8(version_data, st->data[i]);
		}
	}

	/* success */
	return g_steal_pointer(&version_data);
}

static gboolean
fu_algoltek_usb_device_en(FuAlgoltekUsbDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_address_pkt_new();

	fu_struct_algoltek_cmd_address_pkt_set_len(st, 3);
	fu_struct_algoltek_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_CMD_EN);
	fu_struct_algoltek_cmd_address_pkt_set_checksum(st, ~fu_sum8(st->data, st->len) + 1);

	if (!fu_algoltek_usb_device_ctrl_transfer(self,
						  FU_USB_DIRECTION_HOST_TO_DEVICE,
						  FU_ALGOLTEK_CMD_EN,
						  0,
						  0,
						  st,
						  st->data[0],
						  error)) {
		g_prefix_error(error, "system activation failure: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_usb_device_rst(FuAlgoltekUsbDevice *self, guint16 address, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_address_pkt_new();

	fu_struct_algoltek_cmd_address_pkt_set_len(st, 4);
	fu_struct_algoltek_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_CMD_RST);
	fu_struct_algoltek_cmd_address_pkt_set_address(st, address);
	fu_struct_algoltek_cmd_address_pkt_set_checksum(st, ~fu_sum8(st->data, st->len) + 1);

	if (st->data[0] > st->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "rst length invalid, 0x%x > 0x%x",
			    st->data[0],
			    st->len);
		return FALSE;
	}
	if (!fu_algoltek_usb_device_ctrl_transfer(self,
						  FU_USB_DIRECTION_HOST_TO_DEVICE,
						  FU_ALGOLTEK_CMD_RST,
						  0,
						  0,
						  st,
						  st->data[0],
						  error)) {
		g_prefix_error(error, "system reboot failure: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_usb_device_wrr(FuAlgoltekUsbDevice *self, int address, int value, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_address_pkt_new();

	fu_struct_algoltek_cmd_address_pkt_set_len(st, 7);
	fu_struct_algoltek_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_CMD_WRR);
	fu_struct_algoltek_cmd_address_pkt_set_address(st, address);
	fu_struct_algoltek_cmd_address_pkt_set_value(st, value);
	fu_struct_algoltek_cmd_address_pkt_set_checksum(st, ~fu_sum8(st->data, st->len) + 1);

	if (st->data[0] > st->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrr length invalid, 0x%x > 0x%x",
			    st->data[0],
			    st->len);
		return FALSE;
	}
	if (!fu_algoltek_usb_device_ctrl_transfer(self,
						  FU_USB_DIRECTION_HOST_TO_DEVICE,
						  FU_ALGOLTEK_CMD_WRR,
						  0,
						  0,
						  st,
						  st->data[0],
						  error)) {
		g_prefix_error(error, "data write failure: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_usb_device_isp(FuAlgoltekUsbDevice *self,
			   GInputStream *stream,
			   guint address,
			   FuProgress *progress,
			   GError **error)
{
	guint8 basic_data_size = 5;
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream,
						address,
						FU_CHUNK_PAGESZ_NONE,
						64 - basic_data_size,
						error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_transfer_pkt_new();

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		fu_struct_algoltek_cmd_transfer_pkt_set_len(st,
							    basic_data_size +
								fu_chunk_get_data_sz(chk));
		fu_struct_algoltek_cmd_transfer_pkt_set_cmd(st, FU_ALGOLTEK_CMD_ISP);
		fu_struct_algoltek_cmd_transfer_pkt_set_address(st, fu_chunk_get_address(chk));
		if (!fu_struct_algoltek_cmd_transfer_pkt_set_data(st,
								  fu_chunk_get_data(chk),
								  fu_chunk_get_data_sz(chk),
								  error)) {
			g_prefix_error(error, "assign isp data failure: ");
			return FALSE;
		}
		fu_struct_algoltek_cmd_transfer_pkt_set_checksum(st,
								 ~fu_sum8(st->data, st->len) + 1);
		if (st->data[0] > st->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "isp length invalid, 0x%x > 0x%x",
				    st->data[0],
				    st->len);
			return FALSE;
		}
		if (!fu_algoltek_usb_device_ctrl_transfer(self,
							  FU_USB_DIRECTION_HOST_TO_DEVICE,
							  FU_ALGOLTEK_CMD_ISP,
							  0,
							  0,
							  st,
							  st->data[0],
							  error)) {
			g_prefix_error(error, "isp failure: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_algoltek_usb_device_bot(FuAlgoltekUsbDevice *self, int address, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_address_pkt_new();

	fu_struct_algoltek_cmd_address_pkt_set_len(st, 5);
	fu_struct_algoltek_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_CMD_BOT);
	fu_struct_algoltek_cmd_address_pkt_set_address(st, address);
	fu_struct_algoltek_cmd_address_pkt_set_checksum(st, ~fu_sum8(st->data, st->len) + 1);

	if (st->data[0] > st->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "bot length invalid, 0x%x > 0x%x",
			    st->data[0],
			    st->len);
		return FALSE;
	}
	if (!fu_algoltek_usb_device_ctrl_transfer(self,
						  FU_USB_DIRECTION_HOST_TO_DEVICE,
						  FU_ALGOLTEK_CMD_BOT,
						  0,
						  0,
						  st,
						  st->data[0],
						  error)) {
		g_prefix_error(error, "system boot failure: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_usb_device_ers(FuAlgoltekUsbDevice *self,
			   guint erase_type,
			   guint8 sector,
			   GError **error)
{
	guint16 value;
	g_autoptr(GByteArray) st = fu_struct_algoltek_cmd_address_pkt_new();

	fu_struct_algoltek_cmd_address_pkt_set_len(st, 3);
	fu_struct_algoltek_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_CMD_ERS);
	fu_struct_algoltek_cmd_address_pkt_set_checksum(st, ~fu_sum8(st->data, st->len) + 1);

	value = (erase_type << 8) | sector;
	if (!fu_algoltek_usb_device_ctrl_transfer(self,
						  FU_USB_DIRECTION_HOST_TO_DEVICE,
						  FU_ALGOLTEK_CMD_ERS,
						  value,
						  0,
						  st,
						  st->len,
						  error)) {
		g_prefix_error(error, "data clear failure: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_algoltek_usb_device_status_check_cb(FuDevice *self, gpointer user_data, GError **error)
{
	guint8 update_status;
	g_autoptr(GByteArray) update_status_array = NULL;

	update_status_array =
	    fu_algoltek_usb_device_rdr(FU_ALGOLTEK_USB_DEVICE(self), AG_UPDATE_STATUS, error);
	if (update_status_array == NULL)
		return FALSE;

	update_status = update_status_array->data[0];

	switch (update_status) {
	case AG_UPDATE_PASS:
		break;
	case AG_UPDATE_FAIL:
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "update procedure is failed.");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_algoltek_usb_device_wrf(FuAlgoltekUsbDevice *self,
			   GInputStream *stream,
			   FuProgress *progress,
			   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GByteArray) buf_parameter = g_byte_array_new();

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						64,
						error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		guint16 value;
		guint16 index;
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) buf = g_byte_array_new();

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

		fu_byte_array_set_size(buf_parameter, 4, 0);
		if ((i + 1) % 4 == 0)
			buf_parameter->data[0] = 1;
		else
			buf_parameter->data[0] = 0;

		fu_memwrite_uint24(buf_parameter->data + 1,
				   fu_chunk_get_address(chk),
				   G_BIG_ENDIAN);

		value = fu_memread_uint16(buf_parameter->data, G_BIG_ENDIAN);
		index = fu_memread_uint16(buf_parameter->data + 2, G_BIG_ENDIAN);

		if (!fu_algoltek_usb_device_ctrl_transfer(self,
							  FU_USB_DIRECTION_HOST_TO_DEVICE,
							  FU_ALGOLTEK_CMD_WRF,
							  value,
							  index,
							  buf,
							  buf->len,
							  error)) {
			g_prefix_error(error, "data write failure: ");
			return FALSE;
		}

		if ((i + 1) % 4 == 0 || (i + 1) == fu_chunk_array_length(chunks)) {
			if (!fu_device_retry_full(FU_DEVICE(self),
						  fu_algoltek_usb_device_status_check_cb,
						  10,
						  0,
						  NULL,
						  error))
				return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_algoltek_usb_device_setup(FuDevice *device, GError **error)
{
	FuAlgoltekUsbDevice *self = FU_ALGOLTEK_USB_DEVICE(device);
	g_autofree gchar *version_str = NULL;
	g_autoptr(GByteArray) version_data = NULL;

	/* UsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_algoltek_usb_device_parent_class)->setup(device, error))
		return FALSE;

	version_data = fu_algoltek_usb_device_rdv(self, error);
	if (version_data == NULL)
		return FALSE;
	version_str = fu_strsafe((const gchar *)version_data->data, version_data->len);

	fu_device_set_version(device, version_str);

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usb_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuAlgoltekUsbDevice *self = FU_ALGOLTEK_USB_DEVICE(device);
	g_autoptr(GInputStream) stream_isp = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 18, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);

	if (!fu_algoltek_usb_device_en(self, error))
		return FALSE;

	if (!fu_algoltek_usb_device_rst(self, 0x200, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 900);

	if (!fu_algoltek_usb_device_wrr(self, 0x80AD, 0, error))
		return FALSE;

	if (!fu_algoltek_usb_device_wrr(self, 0x80C0, 0, error))
		return FALSE;

	if (!fu_algoltek_usb_device_wrr(self, 0x80C9, 0, error))
		return FALSE;

	if (!fu_algoltek_usb_device_wrr(self, 0x80D1, 0, error))
		return FALSE;

	if (!fu_algoltek_usb_device_wrr(self, 0x80D9, 0, error))
		return FALSE;

	if (!fu_algoltek_usb_device_wrr(self, 0x80E1, 0, error))
		return FALSE;

	if (!fu_algoltek_usb_device_wrr(self, 0x80E9, 0, error))
		return FALSE;

	if (!fu_algoltek_usb_device_rst(self, 0, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 500);

	/* get ISP image */
	stream_isp = fu_firmware_get_image_by_id_stream(firmware, "isp", error);
	if (stream_isp == NULL)
		return FALSE;
	if (!fu_algoltek_usb_device_isp(self,
					stream_isp,
					AG_ISP_ADDR,
					fu_progress_get_child(progress),
					error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_algoltek_usb_device_bot(self, AG_ISP_ADDR, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 1000);

	if (!fu_algoltek_usb_device_ers(self, 0x20, AG_IDENTIFICATION_128K_ADDR, error))
		return FALSE;

	/* preserves compatibility with existing emulation data */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		if (!fu_algoltek_usb_device_ers(self, 0x20, 63, error))
			return FALSE;
		for (guint i = 0; i < 64; i++) {
			if (!fu_algoltek_usb_device_ers(self, 0x20, i, error))
				return FALSE;
		}
	} else if (fu_device_has_private_flag(device,
					      FU_ALGOLTEK_USB_DEVICE_FLAG_ERS_SKIP_FIRST_SECTOR)) {
		/* 1 sector = 4 kb, 128kb = 32 sector */
		for (int i = 1; i < 31; i++) {
			if (!fu_algoltek_usb_device_ers(self, 0x20, i, error))
				return FALSE;
		}
	} else {
		if (!fu_algoltek_usb_device_ers(self, 0x60, 0, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	fu_device_sleep(FU_DEVICE(self), 500);

	/* get payload image */
	stream_payload =
	    fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_algoltek_usb_device_wrf(self,
					stream_payload,
					fu_progress_get_child(progress),
					error))
		return FALSE;

	fu_progress_step_done(progress);

	if (!fu_algoltek_usb_device_rst(self, 0x100, error))
		return FALSE;

	/* the device automatically reboots */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success! */
	return TRUE;
}

static void
fu_algoltek_usb_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_algoltek_usb_device_init(FuAlgoltekUsbDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.algoltek.usb");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_ALGOLTEK_USB_DEVICE_FLAG_ERS_SKIP_FIRST_SECTOR);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ALGOLTEK_USB_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
}

static void
fu_algoltek_usb_device_class_init(FuAlgoltekUsbDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_algoltek_usb_device_setup;
	device_class->write_firmware = fu_algoltek_usb_device_write_firmware;
	device_class->set_progress = fu_algoltek_usb_device_set_progress;
}
