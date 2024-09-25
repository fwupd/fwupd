/*
 * Copyright 2023 GN Audio
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-jabra-file-device.h"
#include "fu-jabra-file-firmware.h"
#include "fu-jabra-file-struct.h"

#define FU_JABRA_FILE_FIRST_BLOCK	       0x00
#define FU_JABRA_FILE_NEXT_BLOCK	       0x01
#define FU_JABRA_FILE_CANCEL		       0x02
#define FU_JABRA_FILE_MAX_RETRIES	       3
#define FU_JABRA_FILE_RETRY_DELAY	       100  /* ms */
#define FU_JABRA_FILE_STANDARD_SEND_TIMEOUT    3000 /* ms */
#define FU_JABRA_FILE_STANDARD_RECEIVE_TIMEOUT 1000 /* ms */

struct _FuJabraFileDevice {
	FuHidDevice parent_instance;
	guint8 sequence_number;
	guint8 address;
	guint dfu_pid;
};

G_DEFINE_TYPE(FuJabraFileDevice, fu_jabra_file_device, FU_TYPE_HID_DEVICE)

static void
fu_jabra_file_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "SequenceNumber", self->sequence_number);
	fwupd_codec_string_append_hex(str, idt, "Address", self->address);
	fwupd_codec_string_append_hex(str, idt, "DfuPid", self->dfu_pid);
}

static gboolean
fu_jabra_file_device_tx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	FuJabraFilePacket *cmd_req = (FuJabraFilePacket *)user_data;
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      0x02,
					      cmd_req->data,
					      cmd_req->len,
					      NULL,
					      FU_JABRA_FILE_STANDARD_SEND_TIMEOUT,
					      NULL, /* cancellable */
					      error)) {
		g_prefix_error(error, "failed to write to device: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_file_device_tx(FuJabraFileDevice *self, FuJabraFilePacket *cmd_req, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_jabra_file_device_tx_cb,
				    FU_JABRA_FILE_MAX_RETRIES,
				    FU_JABRA_FILE_RETRY_DELAY,
				    cmd_req,
				    error);
}

static gboolean
fu_jabra_file_device_rx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	FuJabraFilePacket *cmd_rsp = (FuJabraFilePacket *)user_data;

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      0x82,
					      cmd_rsp->data,
					      cmd_rsp->len,
					      NULL,
					      FU_JABRA_FILE_STANDARD_RECEIVE_TIMEOUT,
					      NULL, /* cancellable */
					      error)) {
		g_prefix_error(error, "failed to read from device: ");
		return FALSE;
	}
	if (cmd_rsp->data[2] == self->address && cmd_rsp->data[5] == 0x12 &&
	    cmd_rsp->data[6] == 0x02) {
		/* battery report, ignore and rx again */
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
						      0x82,
						      cmd_rsp->data,
						      cmd_rsp->len,
						      NULL,
						      FU_JABRA_FILE_STANDARD_RECEIVE_TIMEOUT,
						      NULL, /* cancellable */
						      error)) {
			g_prefix_error(error, "failed to read from device: ");
			return FALSE;
		}
	}
	return TRUE;
}

static FuJabraFilePacket *
fu_jabra_file_device_rx(FuJabraFileDevice *self, GError **error)
{
	g_autoptr(FuJabraFilePacket) cmd_rsp = fu_jabra_file_packet_new();
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_file_device_rx_cb,
				  FU_JABRA_FILE_MAX_RETRIES,
				  FU_JABRA_FILE_RETRY_DELAY,
				  cmd_rsp,
				  error))
		return NULL;
	return g_steal_pointer(&cmd_rsp);
}

static gboolean
fu_jabra_file_device_rx_with_sequence_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	FuJabraFilePacket **cmd_rsp_out = (FuJabraFilePacket **)user_data;
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	cmd_rsp = fu_jabra_file_device_rx(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	if (self->sequence_number != cmd_rsp->data[3]) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "sequence_number error -- got 0x%x, expected 0x%x",
			    cmd_rsp->data[3],
			    self->sequence_number);
		return FALSE;
	}

	/* success */
	self->sequence_number += 1;
	*cmd_rsp_out = g_steal_pointer(&cmd_rsp);
	return TRUE;
}

static FuJabraFilePacket *
fu_jabra_file_device_rx_with_sequence(FuJabraFileDevice *self, GError **error)
{
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_file_device_rx_with_sequence_cb,
				  FU_JABRA_FILE_MAX_RETRIES,
				  FU_JABRA_FILE_RETRY_DELAY,
				  &cmd_rsp,
				  error))
		return NULL;
	return g_steal_pointer(&cmd_rsp);
}

static gboolean
fu_jabra_file_device_ensure_name(FuJabraFileDevice *self, GError **error)
{
	g_autofree gchar *name = NULL;
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req, 0x46);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_IDENTITY);
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	cmd_rsp = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	name = fu_memstrsafe(cmd_rsp->data,
			     cmd_rsp->len,
			     FU_JABRA_FILE_PACKET_OFFSET_PAYLOAD + 1,
			     cmd_rsp->len - (FU_JABRA_FILE_PACKET_OFFSET_PAYLOAD + 1),
			     error);
	if (name == NULL)
		return FALSE;
	fu_device_set_name(FU_DEVICE(self), name);
	return TRUE;
}

static gboolean
fu_jabra_file_device_ensure_dfu_pid(FuJabraFileDevice *self, GError **error)
{
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req, 0x46);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_IDENTITY);
	fu_jabra_file_packet_set_sub_cmd(cmd_req, 0x13);
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	cmd_rsp = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	self->dfu_pid =
	    fu_memread_uint16(cmd_rsp->data + FU_JABRA_FILE_PACKET_OFFSET_PAYLOAD, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_jabra_file_device_ensure_version(FuJabraFileDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req, 0x46);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_IDENTITY);
	fu_jabra_file_packet_set_sub_cmd(cmd_req, 0x03);
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	cmd_rsp = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	version = fu_memstrsafe(cmd_rsp->data,
				cmd_rsp->len,
				FU_JABRA_FILE_PACKET_OFFSET_PAYLOAD + 1,
				cmd_rsp->len - (FU_JABRA_FILE_PACKET_OFFSET_PAYLOAD + 1),
				error);
	if (version == NULL)
		return FALSE;
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_jabra_file_device_file_checksum(FuJabraFileDevice *self,
				   gchar *firmware_checksum,
				   gboolean *match,
				   GError **error)
{
	guint8 device_checksum[16] = {0x00};
	guint8 info[] = {0x01 << 6};
	g_autoptr(FuJabraFilePacket) cmd_req1 = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_req2 = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp1 = NULL;
	g_autoptr(FuJabraFilePacket) cmd_rsp2 = NULL;
	g_autoptr(GString) device_checksum_str = g_string_new(NULL);
	g_autoptr(GString) firmware_checksum_str = g_string_new(firmware_checksum);

	fu_jabra_file_packet_set_dst(cmd_req1, self->address);
	fu_jabra_file_packet_set_src(cmd_req1, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req1, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req1, 0x47);
	fu_jabra_file_packet_set_cmd(cmd_req1, FU_JABRA_FILE_PACKET_CMD_FILE);
	fu_jabra_file_packet_set_sub_cmd(cmd_req1, 0x03);
	if (!fu_jabra_file_device_tx(self, cmd_req1, error))
		return FALSE;
	cmd_rsp1 = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp1 == NULL)
		return FALSE;

	fu_jabra_file_packet_set_dst(cmd_req2, self->address);
	fu_jabra_file_packet_set_src(cmd_req2, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req2, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req2, 0x47);
	fu_jabra_file_packet_set_cmd(cmd_req2, FU_JABRA_FILE_PACKET_CMD_FILE);
	fu_jabra_file_packet_set_sub_cmd(cmd_req2, 0x03);
	if (!fu_jabra_file_packet_set_payload(cmd_req2, info, sizeof(info), error))
		return FALSE;
	if (!fu_jabra_file_device_tx(self, cmd_req2, error))
		return FALSE;
	cmd_rsp2 = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp2 == NULL)
		return FALSE;
	if (cmd_rsp2->data[5] == 0xFE) {
		*match = FALSE;
		return TRUE;
	}
	if (!fu_memcpy_safe(device_checksum,
			    sizeof(device_checksum),
			    0,
			    cmd_rsp2->data,
			    cmd_rsp2->len,
			    12,
			    sizeof(device_checksum),
			    error))
		return FALSE;
	for (guint i = 0; i < sizeof(device_checksum); i++)
		g_string_append_printf(device_checksum_str, "%02x", device_checksum[i]);
	*match = g_string_equal(device_checksum_str, firmware_checksum_str);
	return TRUE;
}

static gboolean
fu_jabra_file_device_write_delete_file(FuJabraFileDevice *self, GError **error)
{
	guint8 data[] = {
	    FU_JABRA_FILE_FIRST_BLOCK << 6 | 11,
	    'u',
	    'p',
	    'g',
	    'r',
	    'a',
	    'd',
	    'e',
	    '.',
	    'z',
	    'i',
	    'p',
	};
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req, 0x80 + 12 + 6);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_FILE);
	fu_jabra_file_packet_set_sub_cmd(cmd_req, 0x04);
	if (!fu_jabra_file_packet_set_payload(cmd_req, data, sizeof(data), error))
		return FALSE;
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	cmd_rsp = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	if (cmd_rsp->data[5] == 0xFE && cmd_rsp->data[6] == 0xF7)
		return TRUE;
	if (cmd_rsp->data[5] == 0xFF)
		return TRUE;

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "internal error: expected 0xFF, got 0x%02x 0x%02x",
		    cmd_rsp->data[5],
		    cmd_rsp->data[6]);
	return FALSE;
}

static gboolean
fu_jabra_file_device_write_first_block(FuJabraFileDevice *self,
				       FuFirmware *firmware,
				       GError **error)
{
	guint8 data[5] = {FU_JABRA_FILE_FIRST_BLOCK << 6 | 15};
	guint8 upgrade[11] = {'u', 'p', 'g', 'r', 'a', 'd', 'e', '.', 'z', 'i', 'p'};
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req, 0x80 + 16 + 6);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_FILE);

	fu_memwrite_uint32(data + 1, fu_firmware_get_size(firmware), G_BIG_ENDIAN);
	if (!fu_jabra_file_packet_set_payload(cmd_req, data, sizeof(data), error))
		return FALSE;
	if (!fu_memcpy_safe(cmd_req->data,
			    cmd_req->len,
			    FU_JABRA_FILE_PACKET_OFFSET_PAYLOAD + sizeof(data),
			    upgrade,
			    sizeof(upgrade),
			    0x0,
			    sizeof(upgrade),
			    error))
		return FALSE;
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	cmd_rsp = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	if (cmd_rsp->data[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    cmd_rsp->data[5],
			    cmd_rsp->data[6]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_jabra_file_device_write_next_block(FuJabraFileDevice *self,
				      guint32 chunk_number,
				      const guint8 *buf,
				      gsize bufsz,
				      GError **error)
{
	guint8 data[2] = {FU_JABRA_FILE_NEXT_BLOCK << 6 | (bufsz + 1), chunk_number};
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req,
					    (((chunk_number + 1) % 101 == 0) ? 0x80 : 0x00) +
						bufsz + 8);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_FILE);
	if (!fu_jabra_file_packet_set_payload(cmd_req, data, sizeof(data), error))
		return FALSE;
	if (!fu_memcpy_safe(cmd_req->data,
			    cmd_req->len,
			    FU_JABRA_FILE_PACKET_OFFSET_PAYLOAD + sizeof(data),
			    buf,
			    bufsz,
			    0x0,
			    bufsz,
			    error))
		return FALSE;
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	if ((chunk_number + 1) % 101 == 0) {
		g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;
		cmd_rsp = fu_jabra_file_device_rx(self, error);
		if (cmd_rsp == NULL)
			return FALSE;
		if (cmd_rsp->data[5] != 0xFF) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "internal error: expected 0xFF, got 0x%02x 0x%02x",
				    cmd_rsp->data[5],
				    cmd_rsp->data[6]);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_file_device_write_blocks(FuJabraFileDevice *self,
				  FuChunkArray *chunks,
				  FuProgress *progress,
				  GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (gint i = 0; (guint)i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_jabra_file_device_write_next_block(self,
							   i,
							   fu_chunk_get_data(chk),
							   fu_chunk_get_data_sz(chk),
							   error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	/* success */
	return TRUE;
}

static gboolean
fu_jabra_file_device_check_device_busy(FuJabraFileDevice *self, GError **error)
{
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req, 0x46);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_VIDEO);
	fu_jabra_file_packet_set_sub_cmd(cmd_req, 0x1D);
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	cmd_rsp = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	if (cmd_rsp->data[7] != 0x00) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED, "is busy");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_file_device_start_update(FuJabraFileDevice *self, GError **error)
{
	guint8 data[] = {0x02};
	g_autoptr(FuJabraFilePacket) cmd_req = fu_jabra_file_packet_new();
	g_autoptr(FuJabraFilePacket) cmd_rsp = NULL;

	fu_jabra_file_packet_set_dst(cmd_req, self->address);
	fu_jabra_file_packet_set_src(cmd_req, 0x00);
	fu_jabra_file_packet_set_sequence_number(cmd_req, self->sequence_number);
	fu_jabra_file_packet_set_cmd_length(cmd_req, 0x87);
	fu_jabra_file_packet_set_cmd(cmd_req, FU_JABRA_FILE_PACKET_CMD_DFU);
	fu_jabra_file_packet_set_sub_cmd(cmd_req, 0x03);
	if (!fu_jabra_file_packet_set_payload(cmd_req, data, sizeof(data), error))
		return FALSE;
	if (!fu_jabra_file_device_tx(self, cmd_req, error))
		return FALSE;
	cmd_rsp = fu_jabra_file_device_rx_with_sequence(self, error);
	if (cmd_rsp == NULL)
		return FALSE;
	if (cmd_rsp->data[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    cmd_rsp->data[5],
			    cmd_rsp->data[6]);
		return FALSE;
	}
	return TRUE;
}

static FuFirmware *
fu_jabra_file_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_jabra_file_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if (fu_jabra_file_firmware_get_dfu_pid(FU_JABRA_FILE_FIRMWARE(firmware)) != self->dfu_pid) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong DFU PID, got 0x%x, expected 0x%x",
			    fu_jabra_file_firmware_get_dfu_pid(FU_JABRA_FILE_FIRMWARE(firmware)),
			    self->dfu_pid);
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_jabra_file_device_setup(FuDevice *device, GError **error)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	if (!fu_jabra_file_device_ensure_name(self, error))
		return FALSE;
	if (!fu_jabra_file_device_ensure_version(self, error))
		return FALSE;
	if (!fu_jabra_file_device_ensure_dfu_pid(self, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_jabra_file_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	const guint chunk_size = 55;
	gboolean match = FALSE;
	g_autofree gchar *firmware_checksum = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* check if firmware file already exists on device */
	firmware_checksum = fu_firmware_get_checksum(firmware, G_CHECKSUM_MD5, error);
	if (!fu_jabra_file_device_file_checksum(self, firmware_checksum, &match, error))
		return FALSE;
	if (!match) {
		/* progress */
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, "first-block");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 89, "next-block");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 10, "update-device");

		/* file not on device, transfer it */
		stream = fu_firmware_get_stream(firmware, error);
		if (stream == NULL)
			return FALSE;
		chunks = fu_chunk_array_new_from_stream(stream, 0x00, chunk_size, error);
		if (chunks == NULL)
			return FALSE;

		if (!fu_jabra_file_device_write_delete_file(self, error))
			return FALSE;
		if (!fu_jabra_file_device_write_first_block(self, firmware, error))
			return FALSE;
		fu_progress_step_done(progress);
		if (!fu_jabra_file_device_write_blocks(self,
						       chunks,
						       fu_progress_get_child(progress),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
		if (!fu_jabra_file_device_file_checksum(self, firmware_checksum, &match, error))
			return FALSE;
		if (!match) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "error transferring file to device, checksum doesn't match");
			return FALSE;
		}
	} else {
		/* file already on device */
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "update-device");
	}

	if (!fu_jabra_file_device_check_device_busy(self, error))
		return FALSE;
	if (!fu_jabra_file_device_start_update(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_file_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuJabraFileDevice *self = FU_JABRA_FILE_DEVICE(device);
	fu_device_sleep_full(FU_DEVICE(self), 900000, progress);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_jabra_file_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 7, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_jabra_file_device_init(FuJabraFileDevice *self)
{
	self->address = 0x01;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_set_remove_delay(FU_DEVICE(self), 120000);
	fu_device_add_protocol(FU_DEVICE(self), "com.jabra.file");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_JABRA_FILE_FIRMWARE);
}

static void
fu_jabra_file_device_class_init(FuJabraFileDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_jabra_file_device_to_string;
	device_class->prepare_firmware = fu_jabra_file_device_prepare_firmware;
	device_class->setup = fu_jabra_file_device_setup;
	device_class->write_firmware = fu_jabra_file_device_write_firmware;
	device_class->attach = fu_jabra_file_device_attach;
	device_class->set_progress = fu_jabra_file_device_set_progress;
}
