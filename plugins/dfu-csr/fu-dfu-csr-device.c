/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-dfu-common.h"
#include "fu-dfu-csr-device.h"
#include "fu-dfu-csr-firmware.h"
#include "fu-dfu-csr-struct.h"
#include "fu-dfu-struct.h"

/**
 * FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY:
 *
 * Respect the write timeout value when performing actions. This is sometimes
 * set to a huge amount of time, and so is not used by default.
 *
 * Since: 1.0.3
 */
#define FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY (1 << 0)

struct _FuDfuCsrDevice {
	FuHidDevice parent_instance;
	FuDfuState dfu_state;
	guint32 dnload_timeout;
};

G_DEFINE_TYPE(FuDfuCsrDevice, fu_dfu_csr_device, FU_TYPE_HID_DEVICE)

#define FU_DFU_CSR_CONTROL_CLEAR_STATUS 0x04
#define FU_DFU_CSR_CONTROL_RESET	0xff

/* maximum firmware packet, including the command header */
#define FU_DFU_CSR_PACKET_DATA_SIZE 1023 /* bytes */

#define FU_DFU_CSR_DEVICE_TIMEOUT 5000 /* ms */

static void
fu_dfu_csr_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE(device);
	fu_string_append(str, idt, "State", fu_dfu_state_to_string(self->dfu_state));
	fu_string_append_ku(str, idt, "DownloadTimeout", self->dnload_timeout);
}

static gboolean
fu_dfu_csr_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	guint8 buf[] = {FU_DFU_CSR_REPORT_ID_CONTROL, FU_DFU_CSR_CONTROL_RESET};
	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      FU_DFU_CSR_REPORT_ID_CONTROL,
				      buf,
				      sizeof(buf),
				      FU_DFU_CSR_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "failed to attach: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dfu_csr_device_get_status(FuDfuCsrDevice *self, GError **error)
{
	guint8 buf[64] = {0};

	/* hit hardware */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_DFU_CSR_REPORT_ID_STATUS,
				      buf,
				      sizeof(buf),
				      FU_DFU_CSR_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_ALLOW_TRUNC |
					  FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "failed to GetStatus: ");
		return FALSE;
	}

	/* check packet */
	if (buf[0] != FU_DFU_CSR_REPORT_ID_STATUS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "GetStatus packet-id was %i expected %i",
			    buf[0],
			    FU_DFU_CSR_REPORT_ID_STATUS);
		return FALSE;
	}

	self->dfu_state = buf[5];
	self->dnload_timeout = fu_memread_uint24(&buf[2], G_LITTLE_ENDIAN);
	g_debug("timeout=%" G_GUINT32_FORMAT, self->dnload_timeout);
	g_debug("state=%s", fu_dfu_state_to_string(self->dfu_state));
	g_debug("status=%s", fu_dfu_status_to_string(buf[6]));
	return TRUE;
}

static gboolean
fu_dfu_csr_device_clear_status(FuDfuCsrDevice *self, GError **error)
{
	guint8 buf[] = {FU_DFU_CSR_REPORT_ID_CONTROL, FU_DFU_CSR_CONTROL_CLEAR_STATUS};

	/* only clear the status if the state is error */
	if (!fu_dfu_csr_device_get_status(self, error))
		return FALSE;
	if (self->dfu_state != FU_DFU_STATE_DFU_ERROR)
		return TRUE;

	/* hit hardware */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_DFU_CSR_REPORT_ID_CONTROL,
				      buf,
				      sizeof(buf),
				      FU_DFU_CSR_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "failed to ClearStatus: ");
		return FALSE;
	}

	/* check the hardware again */
	return fu_dfu_csr_device_get_status(self, error);
}

static GBytes *
fu_dfu_csr_device_upload_chunk(FuDfuCsrDevice *self, GError **error)
{
	guint16 data_sz;
	guint8 buf[64] = {0};

	/* hit hardware */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_DFU_CSR_REPORT_ID_COMMAND,
				      buf,
				      sizeof(buf),
				      FU_DFU_CSR_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_ALLOW_TRUNC |
					  FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "failed to ReadFirmware: ");
		return NULL;
	}

	/* check command byte */
	if (buf[0] != FU_DFU_CSR_REPORT_ID_COMMAND) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "wrong report ID %u", buf[0]);
		return NULL;
	}

	/* check the length */
	data_sz = fu_memread_uint16(&buf[1], G_LITTLE_ENDIAN);
	if (data_sz + FU_STRUCT_DFU_CSR_COMMAND_HEADER_SIZE != (guint16)sizeof(buf)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "wrong data length %" G_GUINT16_FORMAT,
			    data_sz);
		return NULL;
	}

	/* return as bytes */
	return g_bytes_new(buf + FU_STRUCT_DFU_CSR_COMMAND_HEADER_SIZE,
			   sizeof(buf) - FU_STRUCT_DFU_CSR_COMMAND_HEADER_SIZE);
}

static GBytes *
fu_dfu_csr_device_upload(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE(device);
	g_autoptr(GPtrArray) chunks = NULL;
	guint32 total_sz = 0;
	gsize done_sz = 0;

	/* notify UI */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);

	chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	for (guint32 i = 0; i < 0x3ffffff; i++) {
		g_autoptr(GBytes) chunk = NULL;
		gsize chunk_sz;

		/* hit hardware */
		chunk = fu_dfu_csr_device_upload_chunk(self, error);
		if (chunk == NULL)
			return NULL;
		chunk_sz = g_bytes_get_size(chunk);

		/* get the total size using the CSR header */
		if (i == 0) {
			g_autoptr(FuFirmware) firmware = fu_dfu_csr_firmware_new();
			if (!fu_firmware_parse(firmware, chunk, FWUPD_INSTALL_FLAG_NONE, error))
				return NULL;
			total_sz = fu_dfu_csr_firmware_get_total_sz(FU_DFU_CSR_FIRMWARE(firmware));
		}

		/* add to chunk array */
		done_sz += chunk_sz;
		g_ptr_array_add(chunks, g_steal_pointer(&chunk));
		fu_progress_set_percentage_full(progress, done_sz, (gsize)total_sz);

		/* we're done */
		if (chunk_sz < 64 - FU_STRUCT_DFU_CSR_COMMAND_HEADER_SIZE)
			break;
	}

	/* notify UI */
	return fu_dfu_utils_bytes_join_array(chunks);
}

static gboolean
fu_dfu_csr_device_download_chunk(FuDfuCsrDevice *self, guint16 idx, GBytes *chunk, GError **error)
{
	g_autoptr(GByteArray) buf = fu_struct_dfu_csr_command_header_new();

	/* create packet */
	fu_struct_dfu_csr_command_header_set_report_id(buf, FU_DFU_CSR_REPORT_ID_COMMAND);
	fu_struct_dfu_csr_command_header_set_command(buf, FU_DFU_CSR_COMMAND_UPGRADE);
	fu_struct_dfu_csr_command_header_set_idx(buf, idx);
	fu_struct_dfu_csr_command_header_set_chunk_sz(buf, g_bytes_get_size(chunk));
	fu_byte_array_append_bytes(buf, chunk);
	fu_byte_array_set_size(buf, FU_DFU_CSR_PACKET_DATA_SIZE, 0x0);

	/* hit hardware */
	g_debug("writing %" G_GSIZE_FORMAT " bytes of data", g_bytes_get_size(chunk));
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_DFU_CSR_REPORT_ID_COMMAND,
				      buf->data,
				      buf->len,
				      FU_DFU_CSR_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "failed to Upgrade: ");
		return FALSE;
	}

	/* wait for hardware */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY)) {
		g_debug("sleeping for %ums", self->dnload_timeout);
		fu_device_sleep(FU_DEVICE(self), self->dnload_timeout);
	}

	/* get status */
	if (!fu_dfu_csr_device_get_status(self, error))
		return FALSE;

	/* is still busy */
	if (self->dfu_state == FU_DFU_STATE_DFU_DNBUSY) {
		g_debug("busy, so sleeping a bit longer");
		fu_device_sleep(FU_DEVICE(self), 1000);
		if (!fu_dfu_csr_device_get_status(self, error))
			return FALSE;
	}

	/* not correct */
	if (self->dfu_state != FU_DFU_STATE_DFU_DNLOAD_IDLE &&
	    self->dfu_state != FU_DFU_STATE_DFU_IDLE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device did not return to IDLE");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_csr_device_download(FuDevice *device,
			   FuFirmware *firmware,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE(device);
	guint idx;
	g_autoptr(GBytes) blob_empty = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* get default image */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* notify UI */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);

	/* create chunks */
	chunks = fu_chunk_array_new_from_bytes(blob,
					       0x0,
					       FU_DFU_CSR_PACKET_DATA_SIZE -
						   FU_STRUCT_DFU_CSR_COMMAND_HEADER_SIZE);
	if (fu_chunk_array_length(chunks) > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "too many chunks for hardware: 0x%x",
			    fu_chunk_array_length(chunks));
		return FALSE;
	}

	/* send to hardware */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (idx = 0; idx < fu_chunk_array_length(chunks); idx++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, idx);
		g_autoptr(GBytes) blob_tmp = fu_chunk_get_bytes(chk);

		/* send packet */
		if (!fu_dfu_csr_device_download_chunk(self, idx, blob_tmp, error))
			return FALSE;

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* all done */
	blob_empty = g_bytes_new(NULL, 0);
	return fu_dfu_csr_device_download_chunk(self, idx, blob_empty, error);
}

static gboolean
fu_dfu_csr_device_setup(FuDevice *device, GError **error)
{
	FuDfuCsrDevice *self = FU_DFU_CSR_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dfu_csr_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_dfu_csr_device_clear_status(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_dfu_csr_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_dfu_csr_device_init(FuDfuCsrDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.dfu");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ADD_INSTANCE_ID_REV);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_DFU_FIRMWARE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DFU_CSR_DEVICE_FLAG_REQUIRE_DELAY,
					"require-delay");
}

static void
fu_dfu_csr_device_class_init(FuDfuCsrDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_dfu_csr_device_to_string;
	klass_device->write_firmware = fu_dfu_csr_device_download;
	klass_device->dump_firmware = fu_dfu_csr_device_upload;
	klass_device->attach = fu_dfu_csr_device_attach;
	klass_device->setup = fu_dfu_csr_device_setup;
	klass_device->set_progress = fu_dfu_csr_device_set_progress;
}
