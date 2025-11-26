/*
 * Copyright 2023 GN Audio
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-jabra-gnp-child-device.h"
#include "fu-jabra-gnp-common.h"
#include "fu-jabra-gnp-device.h"
#include "fu-jabra-gnp-firmware.h"
#include "fu-jabra-gnp-image.h"

struct _FuJabraGnpChildDevice {
	FuDevice parent_instance;
	guint8 fwu_protocol;
	guint8 sequence_number;
	guint8 address;
	guint16 dfu_pid;
};

G_DEFINE_TYPE(FuJabraGnpChildDevice, fu_jabra_gnp_child_device, FU_TYPE_DEVICE)

static void
fu_jabra_gnp_child_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "FwuProtocol", self->fwu_protocol);
	fwupd_codec_string_append_hex(str, idt, "SequenceNumber", self->sequence_number);
	fwupd_codec_string_append_hex(str, idt, "Address", self->address);
	fwupd_codec_string_append_hex(str, idt, "DfuPid", self->dfu_pid);
}

void
fu_jabra_gnp_child_device_set_dfu_pid_and_seq(FuJabraGnpChildDevice *self, guint16 dfu_pid)
{
	g_return_if_fail(FU_IS_JABRA_GNP_CHILD_DEVICE(self));
	self->dfu_pid = dfu_pid;
	self->sequence_number = 0x00;
}

gboolean
fu_jabra_gnp_child_device_tx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpTxData *tx_data = (FuJabraGnpTxData *)user_data;
	FuJabraGnpDevice *parent = FU_JABRA_GNP_DEVICE(fu_device_get_parent(device));
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(parent),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200 | FU_JABRA_GNP_IFACE,
					    fu_jabra_gnp_device_get_iface_hid(parent),
					    tx_data->txbuf,
					    FU_JABRA_GNP_BUF_SIZE,
					    NULL,
					    tx_data->timeout,
					    NULL, /* cancellable */
					    error)) {
		g_prefix_error_literal(error, "failed to write to device: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_jabra_gnp_child_device_rx_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	const guint8 match_buf[FU_JABRA_GNP_BUF_SIZE] = {
	    FU_JABRA_GNP_IFACE,
	    0x00,
	    self->address,
	    0x00,
	    0x0A,
	    0x12,
	    0x02,
	};
	const guint8 empty_buf[FU_JABRA_GNP_BUF_SIZE] = {0x00};
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;
	FuJabraGnpDevice *parent = FU_JABRA_GNP_DEVICE(fu_device_get_parent(device));

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(parent),
					      fu_jabra_gnp_device_get_epin(parent),
					      rx_data->rxbuf,
					      FU_JABRA_GNP_BUF_SIZE,
					      NULL,
					      rx_data->timeout,
					      NULL, /* cancellable */
					      error)) {
		g_prefix_error_literal(error, "failed to read from device: ");
		return FALSE;
	}
	if (rx_data->rxbuf[2] == match_buf[2] && rx_data->rxbuf[3] == match_buf[3] &&
	    rx_data->rxbuf[5] == match_buf[5] && rx_data->rxbuf[6] == match_buf[6]) {
		/* battery report, ignpre and rx again */
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(parent),
						      0x81,
						      rx_data->rxbuf,
						      FU_JABRA_GNP_BUF_SIZE,
						      NULL,
						      rx_data->timeout,
						      NULL, /* cancellable */
						      error)) {
			g_prefix_error_literal(error, "failed to read from device: ");
			return FALSE;
		}
	}

	if (fu_memcmp_safe(rx_data->rxbuf,
			   sizeof(rx_data->rxbuf),
			   0,
			   empty_buf,
			   sizeof(rx_data->rxbuf),
			   0,
			   sizeof(rx_data->rxbuf),
			   error)) {
		g_prefix_error_literal(error, "error reading from device: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_jabra_gnp_child_device_rx_with_sequence_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	FuJabraGnpRxData *rx_data = (FuJabraGnpRxData *)user_data;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_jabra_gnp_child_device_rx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  rx_data,
				  error))
		return FALSE;
	if (self->sequence_number != rx_data->rxbuf[3]) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "sequence_number error -- got 0x%x, expected 0x%x",
			    rx_data->rxbuf[3],
			    self->sequence_number);
		return FALSE;
	}
	self->sequence_number += 1;
	return TRUE;
}

static FuFirmware *
fu_jabra_gnp_child_device_prepare_firmware(FuDevice *device,
					   GInputStream *stream,
					   FuProgress *progress,
					   FuFirmwareParseFlags flags,
					   GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_jabra_gnp_firmware_new();

	/* unzip and get images */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if (fu_jabra_gnp_firmware_get_dfu_pid(FU_JABRA_GNP_FIRMWARE(firmware)) != self->dfu_pid) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong DFU PID, got 0x%x, expected 0x%x",
			    fu_jabra_gnp_firmware_get_dfu_pid(FU_JABRA_GNP_FIRMWARE(firmware)),
			    self->dfu_pid);
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_jabra_gnp_child_device_setup(FuDevice *device, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);

	if (!fu_jabra_gnp_ensure_name(device, self->address, self->sequence_number, error))
		return FALSE;
	if (!fu_jabra_gnp_ensure_version(device, self->address, self->sequence_number, error))
		return FALSE;
	if (!fu_jabra_gnp_read_dfu_pid(device,
				       self->address,
				       self->sequence_number,
				       &self->dfu_pid,
				       error))
		return FALSE;
	if (!fu_jabra_gnp_ensure_battery_level(device, self->address, self->sequence_number, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_write_image(FuDevice *device,
				      FuFirmware *firmware,
				      FuFirmware *img,
				      FuProgress *progress,
				      GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	const guint chunk_size = 52;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-partition");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, "flash-erase-done");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 91, "write-chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "read-verify-status");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "write-version");

	/* write partition */
	stream = fu_firmware_get_stream(img, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_jabra_gnp_write_partition(device,
					  self->address,
					  self->sequence_number,
					  fu_firmware_get_idx(img),
					  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* start erasing */
	if (!fu_jabra_gnp_start(device, self->address, self->sequence_number, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* poll for erase done */
	if (!fu_jabra_gnp_flash_erase_done(device, self->address, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write chunks */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						chunk_size,
						error);
	if (chunks == NULL)
		return FALSE;
	if (self->fwu_protocol == FU_JABRA_GNP_PROTOCOL_OTA) {
		if (!fu_jabra_gnp_write_crc(device,
					    self->address,
					    self->sequence_number,
					    fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
					    fu_chunk_array_length(chunks),
					    FU_JABRA_GNP_PRELOAD_COUNT,
					    error))
			return FALSE;
	} else {
		/* self->fwu_protocol == FU_JABRA_GNP_PROTOCOL_EXTENDED_OTA*/
		if (!fu_jabra_gnp_write_extended_crc(
			device,
			self->address,
			self->sequence_number,
			fu_jabra_gnp_image_get_crc32(FU_JABRA_GNP_IMAGE(img)),
			fu_chunk_array_length(chunks),
			FU_JABRA_GNP_PRELOAD_COUNT,
			error))
			return FALSE;
	}
	if (!fu_jabra_gnp_write_chunks(device,
				       self->address,
				       chunks,
				       fu_progress_get_child(progress),
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_jabra_gnp_read_verify_status(device, self->address, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write version */
	if (!fu_jabra_gnp_write_version(
		device,
		self->address,
		self->sequence_number,
		fu_jabra_gnp_firmware_get_version_data(FU_JABRA_GNP_FIRMWARE(firmware)),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		fu_progress_add_step(progress,
				     FWUPD_STATUS_UNKNOWN,
				     fu_firmware_get_size(img),
				     fu_firmware_get_id(img));
	}
	if (!fu_jabra_gnp_read_fwu_protocol(device,
					    self->address,
					    self->sequence_number,
					    &self->fwu_protocol,
					    error))
		return FALSE;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_jabra_gnp_child_device_write_image(device,
							   firmware,
							   img,
							   fu_progress_get_child(progress),
							   error)) {
			g_prefix_error(error, "failed to write %s: ", fu_firmware_get_id(img));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* write squif */
	return fu_jabra_gnp_write_dfu_from_squif(device,
						 self->address,
						 self->sequence_number,
						 error);
}

static gboolean
fu_jabra_gnp_child_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);
	fu_device_sleep_full(FU_DEVICE(self), 45000, progress);
	return TRUE;
}

static gboolean
fu_jabra_gnp_child_device_set_quirk_kv(FuDevice *device,
				       const gchar *key,
				       const gchar *value,
				       GError **error)
{
	FuJabraGnpChildDevice *self = FU_JABRA_GNP_CHILD_DEVICE(device);

	if (g_strcmp0(key, "JabraGnpAddress") == 0) {
		guint64 val = 0;
		if (!fu_strtoull(value, &val, 0x0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->address = (guint8)val;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_jabra_gnp_child_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 75, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, "reload");
}

static void
fu_jabra_gnp_child_device_init(FuJabraGnpChildDevice *self)
{
	self->address = FU_JABRA_GNP_ADDRESS_OTA_CHILD;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	/* prohibit to close parent's communication descriptor */
	fu_device_set_logical_id(FU_DEVICE(self), "ota_device");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_protocol(FU_DEVICE(self), "com.jabra.gnp");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_JABRA_GNP_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
}

static void
fu_jabra_gnp_child_device_class_init(FuJabraGnpChildDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_jabra_gnp_child_device_to_string;
	device_class->prepare_firmware = fu_jabra_gnp_child_device_prepare_firmware;
	device_class->setup = fu_jabra_gnp_child_device_setup;
	device_class->write_firmware = fu_jabra_gnp_child_device_write_firmware;
	device_class->attach = fu_jabra_gnp_child_device_attach;
	device_class->set_quirk_kv = fu_jabra_gnp_child_device_set_quirk_kv;
	device_class->set_progress = fu_jabra_gnp_child_device_set_progress;
}
