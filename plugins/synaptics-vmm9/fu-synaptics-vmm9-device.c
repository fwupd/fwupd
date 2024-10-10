/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-vmm9-device.h"
#include "fu-synaptics-vmm9-firmware.h"
#include "fu-synaptics-vmm9-struct.h"

#define FU_SYNAPTICS_VMM9_DEVICE_FLAG_MANUAL_RESTART_REQUIRED "manual-restart-required"

struct _FuSynapticsVmm9Device {
	FuHidDevice parent_instance;
	guint8 board_id;
	guint8 customer_id;
	guint8 active_bank;
};

G_DEFINE_TYPE(FuSynapticsVmm9Device, fu_synaptics_vmm9_device, FU_TYPE_HID_DEVICE)

#define FU_SYNAPTICS_VMM9_DEVICE_REPORT_SIZE 62
#define FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT     5000 /* ms */

#define FU_SYNAPTICS_VMM9_CTRL_BUSY_MASK 0x80
#define FU_SYNAPTICS_VMM9_BUSY_POLL	 10 /* ms */

#define FU_SYNAPTICS_VMM9_MEM_OFFSET_CHIP_SERIAL	0x20200D3C /* 0x4 bytes, %02x */
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_RC_TRIGGER		0x2020A024 /* write 0xF5000000 to reset */
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_MCU_BOOTLOADER_STS 0x2020A030 /* bootloader status */
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_MCU_FW_VERSION	0x2020A038 /* 0x4 bytes, maj.min.mic.? */
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_FIRMWARE_BUILD	0x2020A084 /* 0x4 bytes, be */
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_RC_COMMAND		0x2020B000
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_RC_OFFSET		0x2020B004
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_RC_LENGTH		0x2020B008
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_RC_DATA		0x2020B010 /* until 0x2020B02C */
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_FIRMWARE_NAME	0x90000230 /* 0xF bytes, ASCII */
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_BOARD_ID		0x9000014E /* 0x2 bytes, customer.hardware */

static void
fu_synaptics_vmm9_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "BoardId", self->board_id);
	fwupd_codec_string_append_hex(str, idt, "CustomerId", self->customer_id);
	fwupd_codec_string_append_hex(str, idt, "ActiveBank", self->active_bank);
}

typedef enum {
	FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE = 0,
	FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER = 1 << 0,
	FU_SYNAPTICS_VMM9_COMMAND_FLAG_NO_REPLY = 1 << 1,
	FU_SYNAPTICS_VMM9_COMMAND_FLAG_IGNORE_REPLY = 1 << 2,
} FuSynapticsVmm9DeviceCommandFlags;

typedef struct {
	guint8 *buf;
	gsize bufsz;
} FuSynapticsVmm9DeviceCommandHelper;

static gboolean
fu_synaptics_vmm9_device_command_cb(FuDevice *self, gpointer user_data, GError **error)
{
	FuSynapticsVmm9DeviceCommandHelper *helper =
	    (FuSynapticsVmm9DeviceCommandHelper *)user_data;
	guint8 buf[FU_SYNAPTICS_VMM9_DEVICE_REPORT_SIZE] = {0};
	g_autoptr(FuStructHidGetCommand) st = NULL;
	g_autoptr(FuStructHidPayload) st_payload = NULL;

	/* get, and parse */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_STRUCT_HID_GET_COMMAND_DEFAULT_ID,
				      buf,
				      sizeof(buf),
				      FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}
	st = fu_struct_hid_get_command_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	st_payload = fu_struct_hid_get_command_get_payload(st);
	if (fu_struct_hid_payload_get_sts(st_payload) != FU_SYNAPTICS_VMM9_RC_STS_SUCCESS) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "sts is %s [0x%x]",
		    fu_synaptics_vmm9_rc_sts_to_string(fu_struct_hid_payload_get_sts(st_payload)),
		    fu_struct_hid_payload_get_sts(st_payload));
		return FALSE;
	}

	/* check the busy status */
	if (fu_struct_hid_payload_get_ctrl(st_payload) & FU_SYNAPTICS_VMM9_CTRL_BUSY_MASK) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "is busy");
		return FALSE;
	}

	/* payload is optional */
	if (helper->buf != NULL) {
		gsize fifosz = 0;
		const guint8 *fifo = fu_struct_hid_payload_get_fifo(st_payload, &fifosz);
		if (!fu_memcpy_safe(helper->buf,
				    helper->bufsz,
				    0x0, /* dst */
				    fifo,
				    fifosz,
				    0x0, /*src */
				    helper->bufsz,
				    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_command(FuSynapticsVmm9Device *self,
				 FuSynapticsVmm9RcCtrl ctrl,
				 guint32 offset,
				 const guint8 *src_buf,
				 gsize src_bufsz,
				 guint8 *dst_buf,
				 gsize dst_bufsz,
				 FuSynapticsVmm9DeviceCommandFlags flags,
				 GError **error)
{
	FuSynapticsVmm9DeviceCommandHelper helper = {.buf = dst_buf, .bufsz = dst_bufsz};
	guint8 checksum;
	g_autofree gchar *str = NULL;
	g_autoptr(FuStructHidPayload) st_payload = fu_struct_hid_payload_new();
	g_autoptr(FuStructHidSetCommand) st = fu_struct_hid_set_command_new();
	g_autoptr(GError) error_local = NULL;

	/* payload */
	fu_struct_hid_payload_set_ctrl(st_payload, ctrl | FU_SYNAPTICS_VMM9_CTRL_BUSY_MASK);
	fu_struct_hid_payload_set_offset(st_payload, offset);
	fu_struct_hid_payload_set_length(st_payload, src_bufsz);
	if (src_buf != NULL) {
		if (!fu_struct_hid_payload_set_fifo(st_payload, src_buf, src_bufsz, error))
			return FALSE;
	}

	/* request */
	fu_struct_hid_set_command_set_size(st, FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO + src_bufsz);
	if (!fu_struct_hid_set_command_set_payload(st, st_payload, error))
		return FALSE;
	checksum = 0x100 - fu_sum8(st->data + 1, st->len - 1);
	if (flags & FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER) {
		fu_struct_hid_set_command_set_checksum(st, checksum);
	} else {
		goffset offset_checksum = FU_STRUCT_HID_SET_COMMAND_OFFSET_PAYLOAD +
					  FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO + src_bufsz;
		if (!fu_memwrite_uint8_safe(st->data, st->len, offset_checksum, checksum, error))
			return FALSE;
	}
	fu_byte_array_set_size(st, FU_SYNAPTICS_VMM9_DEVICE_REPORT_SIZE, 0x0);

	/* set */
	str = fu_struct_hid_set_command_to_string(st);
	g_debug("%s", str);
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_STRUCT_HID_SET_COMMAND_DEFAULT_ID,
				      st->data,
				      st->len,
				      FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}

	/* disregard */
	if (flags & FU_SYNAPTICS_VMM9_COMMAND_FLAG_NO_REPLY)
		return TRUE;

	/* need time to complete, no need to poll frequently */
	if (ctrl == FU_SYNAPTICS_VMM9_RC_CTRL_ERASE_FLASH)
		fu_device_sleep(FU_DEVICE(self), 100);

	/* poll for success */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_synaptics_vmm9_device_command_cb,
				  FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT / FU_SYNAPTICS_VMM9_BUSY_POLL,
				  FU_SYNAPTICS_VMM9_BUSY_POLL, /* ms */
				  &helper,
				  &error_local)) {
		if (flags & FU_SYNAPTICS_VMM9_COMMAND_FLAG_IGNORE_REPLY) {
			g_debug("ignoring: %s", error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to poll for success: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_setup(FuDevice *device, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	guint32 mcu_status;
	guint8 buf[4] = {0x0};
	g_autofree gchar *serial = NULL;
	g_autofree gchar *bootloader_version = NULL;
	g_autoptr(FuStructSynapticsUpdGetId) st_getid = NULL;

	/* read chip serial number */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_MEMORY_READ,
					      FU_SYNAPTICS_VMM9_MEM_OFFSET_CHIP_SERIAL,
					      NULL,
					      sizeof(buf),
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
	serial = g_strdup_printf("%02x%02x%02x%02x", buf[0], buf[1], buf[2], buf[3]);
	fu_device_set_serial(device, serial);

	/* read board and customer IDs */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_GET_ID,
					      0x0,
					      NULL,
					      sizeof(buf),
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
	st_getid = fu_struct_synaptics_upd_get_id_parse(buf, sizeof(buf), 0x0, error);
	if (st_getid == NULL)
		return FALSE;
	self->board_id = fu_struct_synaptics_upd_get_id_get_bid(st_getid);
	fu_device_add_instance_u8(device, "BID", self->board_id);
	self->customer_id = fu_struct_synaptics_upd_get_id_get_cid(st_getid);
	fu_device_add_instance_u8(device, "CID", self->customer_id);
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "BID", NULL);
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "BID", "CID", NULL);

	/* whitebox customers */
	if (self->customer_id == 0x0) {
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
	} else {
		g_autofree gchar *vendor_id = g_strdup_printf("0x%02X", self->customer_id);
		fu_device_build_vendor_id(device, "SYNA", vendor_id);
	}

	/* read version */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_MEMORY_READ,
					      FU_SYNAPTICS_VMM9_MEM_OFFSET_MCU_FW_VERSION,
					      NULL,
					      sizeof(buf),
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
	fu_device_set_version_raw(device, fu_memread_uint32(buf, G_BIG_ENDIAN));

	/* read bootloader status */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_MEMORY_READ,
					      FU_SYNAPTICS_VMM9_MEM_OFFSET_MCU_BOOTLOADER_STS,
					      NULL,
					      sizeof(buf),
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
	mcu_status = fu_memread_uint32(buf, G_BIG_ENDIAN);
	if (mcu_status & 1 << 7) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
	self->active_bank = (mcu_status >> 28) & 0b1;
	bootloader_version = g_strdup_printf("0.0.%03u", (guint)(24 >> 28) & 0b1111);
	fu_device_set_version_bootloader(device, bootloader_version);

	/* manual replug required */
	if (fu_device_has_private_flag(device,
				       FU_SYNAPTICS_VMM9_DEVICE_FLAG_MANUAL_RESTART_REQUIRED)) {
		fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
		fu_device_add_request_flag(device, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	} else {
		fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_open(FuDevice *device, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	guint8 payload[] = {'P', 'R', 'I', 'U', 'S'};

	/* HidDevice->open */
	if (!FU_DEVICE_CLASS(fu_synaptics_vmm9_device_parent_class)->open(device, error))
		return FALSE;

	/* unconditionally disable, then enable RC with the magic token */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_DISABLE_RC,
					      0x0, /* offset */
					      NULL,
					      0,
					      NULL,
					      0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NO_REPLY,
					      error)) {
		g_prefix_error(error, "failed to DISABLE_RC before ENABLE_RC: ");
		return FALSE;
	}
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_ENABLE_RC,
					      0x0, /* offset */
					      payload,
					      sizeof(payload),
					      NULL,
					      0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error)) {
		g_prefix_error(error, "failed to ENABLE_RC: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_close(FuDevice *device, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);

	/* no magic token required */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_DISABLE_RC,
					      0, /* offset */
					      NULL,
					      0x0,
					      NULL,
					      0x0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
					      error)) {
		g_prefix_error(error, "failed to DISABLE_RC: ");
		return FALSE;
	}

	/* HidDevice->close */
	if (!FU_DEVICE_CLASS(fu_synaptics_vmm9_device_parent_class)->close(device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_synaptics_vmm9_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_synaptics_vmm9_firmware_new();
	g_autoptr(GInputStream) stream_partial = NULL;

	/* parse */
	stream_partial = fu_partial_input_stream_new(stream,
						     0x0,
						     fu_device_get_firmware_size_min(device),
						     error);
	if (stream_partial == NULL)
		return NULL;
	if (!fu_firmware_parse_stream(firmware, stream_partial, 0x0, flags, error))
		return NULL;

	/* verify this firmware is for this hardware */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		if (self->board_id !=
		    fu_synaptics_vmm9_firmware_get_board_id(FU_SYNAPTICS_VMM9_FIRMWARE(firmware))) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "board ID mismatch, got 0x%02x, expected 0x%02x",
				    fu_synaptics_vmm9_firmware_get_board_id(
					FU_SYNAPTICS_VMM9_FIRMWARE(firmware)),
				    self->board_id);
			return NULL;
		}
		if (self->customer_id != fu_synaptics_vmm9_firmware_get_customer_id(
					     FU_SYNAPTICS_VMM9_FIRMWARE(firmware))) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "customer ID mismatch, got 0x%02x, expected 0x%02x",
				    fu_synaptics_vmm9_firmware_get_customer_id(
					FU_SYNAPTICS_VMM9_FIRMWARE(firmware)),
				    self->customer_id);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_vmm9_device_write_blocks(FuSynapticsVmm9Device *self,
				      FuChunkArray *chunks,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_synaptics_vmm9_device_command(self,
						      FU_SYNAPTICS_VMM9_RC_CTRL_WRITE_FLASH_DATA,
						      fu_chunk_get_address(chk),
						      fu_chunk_get_data(chk),
						      fu_chunk_get_data_sz(chk),
						      NULL,
						      0,
						      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
						      error)) {
			g_prefix_error(error,
				       "failed at page %u, @0x%x",
				       fu_chunk_get_idx(chk),
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_erase(FuSynapticsVmm9Device *self, FuProgress *progress, GError **error)
{
	guint8 buf[2] = {0xFF, 0xFF};
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_ERASE_FLASH,
					      0x0, /* offset */
					      buf,
					      2,
					      NULL,
					      0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
					      error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	return TRUE;
}

static FuFirmware *
fu_synaptics_vmm9_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	gsize bufsz = fu_device_get_firmware_size_min(FU_DEVICE(self));
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf, bufsz, 0, 0x0, FU_STRUCT_HID_PAYLOAD_SIZE_FIFO);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_synaptics_vmm9_device_command(self,
						      FU_SYNAPTICS_VMM9_RC_CTRL_READ_FLASH_DATA,
						      fu_chunk_get_address(chk),
						      NULL,
						      fu_chunk_get_data_sz(chk),
						      fu_chunk_get_data_out(chk),
						      fu_chunk_get_data_sz(chk),
						      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
						      error)) {
			g_prefix_error(error,
				       "failed at chunk %u, @0x%x",
				       fu_chunk_get_idx(chk),
				       (guint)fu_chunk_get_address(chk));
			return NULL;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* parse */
	fw = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	if (!fu_firmware_parse_bytes(firmware, fw, 0x0, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_vmm9_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 3, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);

	/* erase the storage bank */
	if (!fu_synaptics_vmm9_device_erase(self, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* ensure the SPI flash is ready to access the write command */
	fu_device_sleep_full(device, 3000, fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* write each block */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks =
	    fu_chunk_array_new_from_stream(stream, 0x0, FU_STRUCT_HID_PAYLOAD_SIZE_FIFO, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_synaptics_vmm9_device_write_blocks(self,
						   chunks,
						   fu_progress_get_child(progress),
						   error)) {
		g_prefix_error(error, "failed to write: ");
		return FALSE;
	}
	fu_device_sleep(device, 10);
	fu_progress_step_done(progress);

	/* activate the firmware */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_ACTIVATE_FIRMWARE,
					      0x0, /* offset */
					      NULL,
					      0,
					      NULL,
					      0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
					      error)) {
		g_prefix_error(error, "failed to activate: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* generic request */
	if (fu_device_has_private_flag(device,
				       FU_SYNAPTICS_VMM9_DEVICE_FLAG_MANUAL_RESTART_REQUIRED)) {
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REPLUG_POWER);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		if (!fu_device_emit_request(device, request, progress, error))
			return FALSE;
	} else {
		guint8 buf[] = {0xF5, 0x00, 0x00, 0x00};
		/* one register write to exactly the right place :) */
		if (!fu_synaptics_vmm9_device_command(
			self,
			FU_SYNAPTICS_VMM9_RC_CTRL_MEMORY_WRITE,
			FU_SYNAPTICS_VMM9_MEM_OFFSET_RC_TRIGGER,
			buf,
			sizeof(buf),
			NULL,
			0,
			FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER |
			    FU_SYNAPTICS_VMM9_COMMAND_FLAG_IGNORE_REPLY,
			error)) {
			g_prefix_error(error, "failed to reboot: ");
			return FALSE;
		}
	}

	/* success! */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_synaptics_vmm9_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_synaptics_vmm9_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%u.%02u.%03u",
			       (guint)(version_raw >> 16) & 0xFF,
			       (guint)(version_raw >> 24) & 0xFF,
			       (guint)(version_raw >> 8) & 0xFF);
}

static void
fu_synaptics_vmm9_device_init(FuSynapticsVmm9Device *self)
{
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x7F000);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_install_duration(FU_DEVICE(self), 40);
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.mst-hid");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_VMM9_DEVICE_FLAG_MANUAL_RESTART_REQUIRED);
}

static void
fu_synaptics_vmm9_device_class_init(FuSynapticsVmm9DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_synaptics_vmm9_device_to_string;
	device_class->setup = fu_synaptics_vmm9_device_setup;
	device_class->open = fu_synaptics_vmm9_device_open;
	device_class->close = fu_synaptics_vmm9_device_close;
	device_class->prepare_firmware = fu_synaptics_vmm9_device_prepare_firmware;
	device_class->write_firmware = fu_synaptics_vmm9_device_write_firmware;
	device_class->read_firmware = fu_synaptics_vmm9_device_read_firmware;
	device_class->set_progress = fu_synaptics_vmm9_device_set_progress;
	device_class->convert_version = fu_synaptics_vmm9_device_convert_version;
}
