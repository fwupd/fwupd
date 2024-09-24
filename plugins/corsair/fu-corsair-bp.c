/*
 * Copyright 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-corsair-bp.h"
#include "fu-corsair-common.h"

#define CORSAIR_DEFAULT_VENDOR_INTERFACE_ID 1
#define CORSAIR_ACTIVATION_TIMEOUT	    30000
#define CORSAIR_MODE_BOOTLOADER		    3
#define CORSAIR_FIRST_CHUNK_HEADER_SIZE	    7
#define CORSAIR_NEXT_CHUNKS_HEADER_SIZE	    3
#define CORSAIR_TRANSACTION_TIMEOUT	    10000
#define CORSAIR_DEFAULT_CMD_SIZE	    64

#define CORSAIR_OFFSET_CMD_PROPERTY_ID	  0x02
#define CORSAIR_OFFSET_CMD_PROPERTY_VALUE 0x03
#define CORSAIR_OFFSET_CMD_VERSION	  0x03
#define CORSAIR_OFFSET_CMD_CRC		  0x08
#define CORSAIR_OFFSET_CMD_MODE		  0x03
#define CORSAIR_OFFSET_CMD_STATUS	  0x02
#define CORSAIR_OFFSET_CMD_FIRMWARE_SIZE  0x03
#define CORSAIR_OFFSET_CMD_SET_MODE	  0x04
#define CORSAIR_OFFSET_CMD_DESTINATION	  0x00

#define CORSAIR_INPUT_FLUSH_TIMEOUT    10
#define CORSAIR_INPUT_FLUSH_ITERATIONS 3

typedef enum {
	FU_CORSAIR_BP_DESTINATION_SELF = 0x08,
	FU_CORSAIR_BP_DESTINATION_SUBDEVICE = 0x09
} FuCorsairBpDestination;

struct _FuCorsairBp {
	FuUsbDevice parent_instance;
	guint8 destination;
	guint8 epin;
	guint8 epout;
	guint16 cmd_write_size;
	guint16 cmd_read_size;
	gboolean is_legacy_attach;
};
G_DEFINE_TYPE(FuCorsairBp, fu_corsair_bp, FU_TYPE_USB_DEVICE)

static gboolean
fu_corsair_bp_command(FuCorsairBp *self,
		      guint8 *data,
		      guint timeout,
		      gboolean need_reply,
		      GError **error)
{
	gsize actual_len = 0;
	gboolean ret;

	data[CORSAIR_OFFSET_CMD_DESTINATION] = self->destination;

	fu_dump_raw("FuPluginCorsair", "command", data, self->cmd_write_size);

	ret = fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					       self->epout,
					       data,
					       self->cmd_write_size,
					       &actual_len,
					       timeout,
					       NULL,
					       error);
	if (!ret) {
		g_prefix_error(error, "failed to write command: ");
		return FALSE;
	}
	if (actual_len != self->cmd_write_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong size written: %" G_GSIZE_FORMAT,
			    actual_len);
		return FALSE;
	}

	if (!need_reply)
		return TRUE;

	memset(data, 0, FU_CORSAIR_MAX_CMD_SIZE);

	ret = fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					       self->epin,
					       data,
					       self->cmd_read_size,
					       &actual_len,
					       timeout,
					       NULL,
					       error);
	if (!ret) {
		g_prefix_error(error, "failed to get command response: ");
		return FALSE;
	}
	if (actual_len != self->cmd_read_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong size read: %" G_GSIZE_FORMAT,
			    actual_len);
		return FALSE;
	}

	fu_dump_raw("FuPluginCorsair", "response", data, self->cmd_write_size);

	if (data[CORSAIR_OFFSET_CMD_STATUS] != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device replied with error: 0x%02x",
			    data[CORSAIR_OFFSET_CMD_STATUS]);
		return FALSE;
	}

	return TRUE;
}

/**
 * @brief Flush all input reports if there are any.
 * @self: a #FuCorsairBp
 *
 * This function clears any dangling IN reports that
 * the device may have sent after the enumeration.
 */
void
fu_corsair_bp_flush_input_reports(FuCorsairBp *self)
{
	gsize actual_len;
	g_autofree guint8 *buf = g_malloc0(self->cmd_read_size);

	for (guint i = 0; i < CORSAIR_INPUT_FLUSH_ITERATIONS; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
						      self->epin,
						      buf,
						      self->cmd_read_size,
						      &actual_len,
						      CORSAIR_INPUT_FLUSH_TIMEOUT,
						      NULL,
						      &error_local))
			g_debug("flushing status: %s", error_local->message);
	}
}

static gboolean
fu_corsair_bp_write_first_chunk(FuCorsairBp *self,
				FuChunk *chunk,
				guint32 firmware_size,
				GError **error)
{
	guint8 init_cmd[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x0d, 0x00, 0x03};
	guint8 write_cmd[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x06, 0x00};
	if (!fu_corsair_bp_command(self, init_cmd, CORSAIR_TRANSACTION_TIMEOUT, TRUE, error)) {
		g_prefix_error(error, "firmware init fail: ");
		return FALSE;
	}

	if (!fu_memwrite_uint32_safe(write_cmd,
				     sizeof(write_cmd),
				     CORSAIR_OFFSET_CMD_FIRMWARE_SIZE,
				     firmware_size,
				     G_LITTLE_ENDIAN,
				     error)) {
		g_prefix_error(error, "cannot serialize firmware size: ");
		return FALSE;
	}
	if (!fu_memcpy_safe(write_cmd,
			    sizeof(write_cmd),
			    CORSAIR_FIRST_CHUNK_HEADER_SIZE,
			    fu_chunk_get_data(chunk),
			    fu_chunk_get_data_sz(chunk),
			    0,
			    fu_chunk_get_data_sz(chunk),
			    error)) {
		g_prefix_error(error, "cannot set data: ");
		return FALSE;
	}
	if (!fu_corsair_bp_command(self, write_cmd, CORSAIR_TRANSACTION_TIMEOUT, TRUE, error)) {
		g_prefix_error(error, "write command fail: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_corsair_bp_write_chunk(FuCorsairBp *self, FuChunk *chunk, GError **error)
{
	guint8 cmd[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x07};
	if (!fu_memcpy_safe(cmd,
			    sizeof(cmd),
			    CORSAIR_NEXT_CHUNKS_HEADER_SIZE,
			    fu_chunk_get_data(chunk),
			    fu_chunk_get_data_sz(chunk),
			    0,
			    fu_chunk_get_data_sz(chunk),
			    error)) {
		g_prefix_error(error, "cannot set data: ");
		return FALSE;
	}
	if (!fu_corsair_bp_command(self, cmd, CORSAIR_TRANSACTION_TIMEOUT, TRUE, error)) {
		g_prefix_error(error, "write command fail: ");
		return FALSE;
	}
	return TRUE;
}

static void
fu_corsair_bp_incorporate(FuDevice *self, FuDevice *donor)
{
	FuCorsairBp *bp_self = FU_CORSAIR_BP(self);
	FuCorsairBp *bp_donor = FU_CORSAIR_BP(donor);

	g_return_if_fail(FU_IS_CORSAIR_BP(self));
	g_return_if_fail(FU_IS_CORSAIR_BP(donor));

	bp_self->epin = bp_donor->epin;
	bp_self->epout = bp_donor->epout;
	bp_self->cmd_write_size = bp_donor->cmd_write_size;
	bp_self->cmd_read_size = bp_donor->cmd_read_size;
}

static void
fu_corsair_bp_init(FuCorsairBp *self)
{
	self->cmd_read_size = CORSAIR_DEFAULT_CMD_SIZE;
	self->cmd_write_size = CORSAIR_DEFAULT_CMD_SIZE;
	self->destination = FU_CORSAIR_BP_DESTINATION_SELF;
}

gboolean
fu_corsair_bp_get_property(FuCorsairBp *self,
			   FuCorsairBpProperty property,
			   guint32 *value,
			   GError **error)
{
	guint8 data[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x02};

	fu_memwrite_uint16(&data[CORSAIR_OFFSET_CMD_PROPERTY_ID],
			   (guint16)property,
			   G_LITTLE_ENDIAN);

	if (!fu_corsair_bp_command(self, data, CORSAIR_TRANSACTION_TIMEOUT, TRUE, error))
		return FALSE;

	*value = fu_memread_uint32(&data[CORSAIR_OFFSET_CMD_PROPERTY_VALUE], G_LITTLE_ENDIAN);

	return TRUE;
}

static gboolean
fu_corsair_bp_set_mode(FuCorsairBp *self, FuCorsairDeviceMode mode, GError **error)
{
	guint8 cmd[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x01, 0x03};

	cmd[CORSAIR_OFFSET_CMD_SET_MODE] = mode;

	if (!fu_corsair_bp_command(self, cmd, CORSAIR_TRANSACTION_TIMEOUT, TRUE, error)) {
		g_prefix_error(error, "set mode command fail: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_corsair_bp_write_firmware_chunks(FuCorsairBp *self,
				    FuChunk *first_chunk,
				    FuChunkArray *chunks,
				    FuProgress *progress,
				    guint32 firmware_size,
				    GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks) + 1);

	if (!fu_corsair_bp_write_first_chunk(self, first_chunk, firmware_size, error)) {
		g_prefix_error(error, "cannot write first chunk: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_corsair_bp_write_chunk(self, chk, error)) {
			g_prefix_error(error, "cannot write chunk %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_corsair_bp_commit_firmware(FuCorsairBp *self, GError **error)
{
	guint8 commit_cmd[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x05, 0x01, 0x00};
	if (!fu_corsair_bp_command(self, commit_cmd, CORSAIR_TRANSACTION_TIMEOUT, TRUE, error)) {
		g_prefix_error(error, "firmware commit fail: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_corsair_bp_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	const guint8 *firmware_raw;
	gsize firmware_size;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuChunk) firstChunk = NULL;
	g_autoptr(GBytes) rest_of_firmware = NULL;
	FuCorsairBp *self = FU_CORSAIR_BP(device);
	guint32 first_chunk_size = self->cmd_write_size - CORSAIR_FIRST_CHUNK_HEADER_SIZE;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL) {
		g_prefix_error(error, "cannot get firmware data: ");
		return FALSE;
	}
	firmware_raw = fu_bytes_get_data_safe(blob, &firmware_size, error);
	if (firmware_raw == NULL) {
		g_prefix_error(error, "cannot get firmware data: ");
		return FALSE;
	}

	/* the firmware size should be greater than 1 chunk */
	if (firmware_size <= first_chunk_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "update file should be bigger");
		return FALSE;
	}

	firstChunk = fu_chunk_new(0, 0, 0, g_bytes_get_data(blob, NULL), first_chunk_size);
	rest_of_firmware =
	    fu_bytes_new_offset(blob, first_chunk_size, firmware_size - first_chunk_size, error);
	if (rest_of_firmware == NULL) {
		g_prefix_error(error, "cannot get firmware past first chunk: ");
		return FALSE;
	}
	chunks =
	    fu_chunk_array_new_from_bytes(rest_of_firmware,
					  first_chunk_size,
					  self->cmd_write_size - CORSAIR_NEXT_CHUNKS_HEADER_SIZE);

	if (!fu_corsair_bp_write_firmware_chunks(self,
						 firstChunk,
						 chunks,
						 progress,
						 g_bytes_get_size(blob),
						 error))
		return FALSE;

	if (!fu_corsair_bp_commit_firmware(self, error))
		return FALSE;

	return TRUE;
}

gboolean
fu_corsair_bp_activate_firmware(FuCorsairBp *self, FuFirmware *firmware, GError **error)
{
	guint32 crc;
	gsize firmware_size;
	const guint8 *firmware_raw;
	g_autoptr(GBytes) blob = NULL;
	guint8 cmd[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x16, 0x00, 0x01, 0x03, 0x00, 0x01, 0x01};

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL) {
		g_prefix_error(error, "cannot get firmware bytes: ");
		return FALSE;
	}

	firmware_raw = fu_bytes_get_data_safe(blob, &firmware_size, error);
	if (firmware_raw == NULL) {
		g_prefix_error(error, "cannot get firmware data: ");
		return FALSE;
	}

	crc = fu_crc32(FU_CRC_KIND_B32_MPEG2, firmware_raw, firmware_size);
	fu_memwrite_uint32(&cmd[CORSAIR_OFFSET_CMD_CRC], crc, G_LITTLE_ENDIAN);

	return fu_corsair_bp_command(self, cmd, CORSAIR_ACTIVATION_TIMEOUT, TRUE, error);
}

static gboolean
fu_corsair_bp_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCorsairBp *self = FU_CORSAIR_BP(device);
	if (self->is_legacy_attach) {
		guint8 cmd[FU_CORSAIR_MAX_CMD_SIZE] = {0x08, 0x10, 0x01, 0x00, 0x03, 0x00, 0x01};
		return fu_corsair_bp_command(self, cmd, CORSAIR_TRANSACTION_TIMEOUT, FALSE, error);
	}
	return fu_corsair_bp_set_mode(self, FU_CORSAIR_DEVICE_MODE_APPLICATION, error);
}

static gboolean
fu_corsair_bp_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCorsairBp *self = FU_CORSAIR_BP(device);
	return fu_corsair_bp_set_mode(self, FU_CORSAIR_DEVICE_MODE_BOOTLOADER, error);
}

static void
fu_corsair_bp_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCorsairBp *self = FU_CORSAIR_BP(device);
	fwupd_codec_string_append_hex(str, idt, "InEndpoint", self->epin);
	fwupd_codec_string_append_hex(str, idt, "OutEndpoint", self->epout);
}

static void
fu_corsair_bp_class_init(FuCorsairBpClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->incorporate = fu_corsair_bp_incorporate;
	device_class->write_firmware = fu_corsair_bp_write_firmware;
	device_class->attach = fu_corsair_bp_attach;
	device_class->detach = fu_corsair_bp_detach;
	device_class->to_string = fu_corsair_bp_to_string;
}

FuCorsairBp *
fu_corsair_bp_new(FuUsbDevice *usb_device, gboolean is_subdevice)
{
	FuCorsairBp *self = g_object_new(FU_TYPE_CORSAIR_BP, NULL);

	fu_device_incorporate(FU_DEVICE(self),
			      FU_DEVICE(usb_device),
			      FU_DEVICE_INCORPORATE_FLAG_ALL);
	if (is_subdevice) {
		self->destination = FU_CORSAIR_BP_DESTINATION_SUBDEVICE;
	} else {
		self->destination = FU_CORSAIR_BP_DESTINATION_SELF;
	}

	return self;
}

void
fu_corsair_bp_set_cmd_size(FuCorsairBp *self, guint16 write_size, guint16 read_size)
{
	self->cmd_write_size = write_size;
	self->cmd_read_size = read_size;
}

void
fu_corsair_bp_set_endpoints(FuCorsairBp *self, guint8 epin, guint8 epout)
{
	self->epin = epin;
	self->epout = epout;
}

void
fu_corsair_bp_set_legacy_attach(FuCorsairBp *self, gboolean is_legacy_attach)
{
	self->is_legacy_attach = is_legacy_attach;
}
