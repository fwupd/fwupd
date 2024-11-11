/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-telink-dfu-archive.h"
#include "fu-telink-dfu-hid-device.h"
#include "fu-telink-dfu-struct.h"

struct _FuTelinkDfuHidDevice {
	FuHidDevice parent_instance;
	guint16 windows_hid_tool_ver;
};

G_DEFINE_TYPE(FuTelinkDfuHidDevice, fu_telink_dfu_hid_device, FU_TYPE_HID_DEVICE)

#define FU_TELINK_DFU_HID_DEVICE_START_ADDR	    0x0000
#define FU_TELINK_DFU_HID_DEVICE_REPORT_TIMEOUT	    500 /* ms */
#define FU_TELINK_DFU_HID_DEVICE_OTA_LENGTH	    65
#define FU_TELINK_DFU_HID_DEVICE_OTA_START_LEN	    2
#define FU_TELINK_DFU_HID_DEVICE_OTA_END_LEN	    6
#define FU_TELINK_DFU_HID_DEVICE_OTA_DATA_LEN	    20
#define FU_TELINK_DFU_HID_DEVICE_REPORT_ID	    6
#define FU_TELINK_DFU_HID_EP_IN			    (0x80 | 4)
#define FU_TELINK_DFU_HID_EP_OUT		    (0x00 | 5)
#define FU_TELINK_DEVICE_WINDOWS_TOOL_VERSION(a, b) ((a) * 100 + (b))

static void
fu_telink_dfu_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "WindowsHidToolVer", self->windows_hid_tool_ver);
}

static FuStructTelinkDfuHidPkt *
fu_telink_dfu_hid_device_create_packet(FuTelinkDfuCmd cmd,
				       const guint8 *buf,
				       gsize bufsz,
				       GError **error)
{
	guint16 ota_data_len;
	g_autoptr(FuStructTelinkDfuHidPkt) st_pkt = fu_struct_telink_dfu_hid_pkt_new();
	g_autoptr(FuStructTelinkDfuHidPkt) st_payload = fu_struct_telink_dfu_hid_pkt_payload_new();

	switch (cmd) {
	case FU_TELINK_DFU_CMD_OTA_FW_VERSION:
		ota_data_len = 0;
		break;
	case FU_TELINK_DFU_CMD_OTA_START:
		ota_data_len = FU_TELINK_DFU_HID_DEVICE_OTA_START_LEN;
		break;
	case FU_TELINK_DFU_CMD_OTA_END:
		ota_data_len = FU_TELINK_DFU_HID_DEVICE_OTA_END_LEN;
		break;
	default:
		/* OTA data */
		ota_data_len = FU_TELINK_DFU_HID_DEVICE_OTA_DATA_LEN;
		break;
	}

	fu_struct_telink_dfu_hid_pkt_payload_set_ota_cmd(st_payload, cmd);
	if (buf != NULL) {
		if (!fu_struct_telink_dfu_hid_pkt_payload_set_ota_data(st_payload,
								       buf,
								       bufsz,
								       error)) {
			return NULL;
		}
	}

	/* exclude the ota_cmd field */
	fu_struct_telink_dfu_hid_pkt_payload_set_crc(
	    st_payload,
	    ~fu_crc16(FU_CRC_KIND_B16_USB, st_payload->data, st_payload->len - 2));
	fu_struct_telink_dfu_hid_pkt_set_ota_data_len(st_pkt, ota_data_len);
	if (!fu_struct_telink_dfu_hid_pkt_set_payload(st_pkt, st_payload, error))
		return NULL;

	return g_steal_pointer(&st_pkt);
}

static gboolean
fu_telink_dfu_hid_device_write(FuTelinkDfuHidDevice *self,
			       const guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	FuHidDeviceFlags set_report_flag = FU_HID_DEVICE_FLAG_NONE;
	guint8 buf_mut[FU_TELINK_DFU_HID_DEVICE_OTA_LENGTH] = {0};

	if (self->windows_hid_tool_ver >= FU_TELINK_DEVICE_WINDOWS_TOOL_VERSION(5, 2))
		set_report_flag = FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER;

	if (!fu_memcpy_safe(buf_mut, sizeof(buf_mut), 0x0, buf, bufsz, 0x0, bufsz, error))
		return FALSE;
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					FU_TELINK_DFU_HID_DEVICE_REPORT_ID,
					buf_mut,
					sizeof(buf_mut),
					FU_TELINK_DFU_HID_DEVICE_REPORT_TIMEOUT,
					set_report_flag,
					error);
}

static gboolean
fu_telink_dfu_hid_device_write_blocks(FuTelinkDfuHidDevice *self,
				      FuChunkArray *chunks,
				      FuProgress *progress,
				      GError **error)
{
	guint payload_index = 0;
	g_autoptr(FuStructTelinkDfuHidLongPkt) st_long_pkt =
	    fu_struct_telink_dfu_hid_long_pkt_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuStructTelinkDfuHidPkt) st_pkt = NULL;

		/* send chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		st_pkt = fu_telink_dfu_hid_device_create_packet((guint16)i,
								fu_chunk_get_data(chk),
								fu_chunk_get_data_sz(chk),
								error);
		if (st_pkt == NULL)
			return FALSE;

		if (self->windows_hid_tool_ver >= FU_TELINK_DEVICE_WINDOWS_TOOL_VERSION(5, 2)) {
			FuStructTelinkDfuHidPktPayload *st_payload =
			    fu_struct_telink_dfu_hid_pkt_get_payload(st_pkt);
			/* TODO: find a better method to declare the structure array */
			payload_index = i % 3;
			fu_struct_telink_dfu_hid_long_pkt_set_ota_data_len(
			    st_long_pkt,
			    FU_TELINK_DFU_HID_DEVICE_OTA_DATA_LEN * (payload_index + 1));
			if (payload_index == 0) {
				FuStructTelinkDfuHidPktPayload *st_default_payload =
				    fu_struct_telink_dfu_hid_pkt_payload_new();
				if (!fu_struct_telink_dfu_hid_long_pkt_set_payload_1(st_long_pkt,
										     st_payload,
										     error))
					return FALSE;

				if (!fu_struct_telink_dfu_hid_long_pkt_set_payload_2(
					st_long_pkt,
					st_default_payload,
					error))
					return FALSE;
				if (!fu_struct_telink_dfu_hid_long_pkt_set_payload_3(
					st_long_pkt,
					st_default_payload,
					error))
					return FALSE;
			} else if (payload_index == 1) {
				if (!fu_struct_telink_dfu_hid_long_pkt_set_payload_2(st_long_pkt,
										     st_payload,
										     error))
					return FALSE;
			} else if (payload_index == 2) {
				if (!fu_struct_telink_dfu_hid_long_pkt_set_payload_3(st_long_pkt,
										     st_payload,
										     error))
					return FALSE;
				if (!fu_telink_dfu_hid_device_write(self,
								    st_long_pkt->data,
								    st_long_pkt->len,
								    error))
					return FALSE;
			} else {
				/* should not reach here */
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "wrong payload index");
				return FALSE;
			}
		} else {
			if (!fu_telink_dfu_hid_device_write(self, st_pkt->data, st_pkt->len, error))
				return FALSE;
			fu_device_sleep(FU_DEVICE(self), 20);
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	if (self->windows_hid_tool_ver >= FU_TELINK_DEVICE_WINDOWS_TOOL_VERSION(5, 2) &&
	    payload_index != 2) {
		if (!fu_telink_dfu_hid_device_write(self,
						    st_long_pkt->data,
						    st_long_pkt->len,
						    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_ota_start(FuTelinkDfuHidDevice *self, GError **error)
{
	g_autoptr(FuStructTelinkDfuHidPkt) st_pkt = NULL;

	st_pkt =
	    fu_telink_dfu_hid_device_create_packet(FU_TELINK_DFU_CMD_OTA_START, NULL, 0, error);
	if (st_pkt == NULL)
		return FALSE;

	if (self->windows_hid_tool_ver >= FU_TELINK_DEVICE_WINDOWS_TOOL_VERSION(5, 2)) {
		g_autoptr(FuStructTelinkDfuHidLongPkt) st_long_pkt =
		    fu_struct_telink_dfu_hid_long_pkt_new();
		g_autoptr(FuStructTelinkDfuHidPktPayload) st_payload =
		    fu_struct_telink_dfu_hid_pkt_get_payload(st_pkt);
		fu_struct_telink_dfu_hid_long_pkt_set_ota_data_len(
		    st_long_pkt,
		    fu_struct_telink_dfu_hid_pkt_get_ota_data_len(st_pkt));
		if (!fu_struct_telink_dfu_hid_long_pkt_set_payload_1(st_long_pkt,
								     st_payload,
								     error))
			return FALSE;
		if (!fu_telink_dfu_hid_device_write(self,
						    st_long_pkt->data,
						    st_long_pkt->len,
						    error))
			return FALSE;
	} else {
		if (!fu_telink_dfu_hid_device_write(self, st_pkt->data, st_pkt->len, error))
			return FALSE;
	}

	/* success */
	fu_device_sleep(FU_DEVICE(self), 20);
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_ota_stop(FuTelinkDfuHidDevice *self, guint number_chunks, GError **error)
{
	guint16 pkt_index = (guint16)(number_chunks)-1;
	g_autoptr(FuStructTelinkDfuEndCheck) st_end_check = fu_struct_telink_dfu_end_check_new();
	g_autoptr(FuStructTelinkDfuHidPkt) st_pkt = NULL;

	/* last data packet index */
	fu_struct_telink_dfu_end_check_set_pkt_index(st_end_check, pkt_index);
	if (self->windows_hid_tool_ver >= FU_TELINK_DEVICE_WINDOWS_TOOL_VERSION(5, 2))
		fu_struct_telink_dfu_end_check_set_inverted_pkt_index(st_end_check, ~pkt_index + 1);
	else
		fu_struct_telink_dfu_end_check_set_inverted_pkt_index(st_end_check, ~pkt_index);
	st_pkt = fu_telink_dfu_hid_device_create_packet(FU_TELINK_DFU_CMD_OTA_END,
							st_end_check->data,
							st_end_check->len,
							error);
	if (st_pkt == NULL)
		return FALSE;
	if (self->windows_hid_tool_ver >= FU_TELINK_DEVICE_WINDOWS_TOOL_VERSION(5, 2)) {
		g_autoptr(FuStructTelinkDfuHidLongPkt) st_long_pkt =
		    fu_struct_telink_dfu_hid_long_pkt_new();
		g_autoptr(FuStructTelinkDfuHidPktPayload) st_payload =
		    fu_struct_telink_dfu_hid_pkt_get_payload(st_pkt);
		fu_struct_telink_dfu_hid_pkt_payload_set_crc(st_payload, 0xFFFF);
		fu_struct_telink_dfu_hid_long_pkt_set_ota_data_len(st_long_pkt, 6);
		if (!fu_struct_telink_dfu_hid_long_pkt_set_payload_1(st_long_pkt,
								     st_payload,
								     error))
			return FALSE;
		if (!fu_telink_dfu_hid_device_write(self,
						    st_long_pkt->data,
						    st_long_pkt->len,
						    error))
			return FALSE;
	} else {
		if (!fu_telink_dfu_hid_device_write(self, st_pkt->data, st_pkt->len, error))
			return FALSE;
	}

	/* success */
	fu_device_sleep(FU_DEVICE(self), 10000);
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_write_blob(FuTelinkDfuHidDevice *self,
				    GBytes *blob,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuStructTelinkDfuHidPkt) st_pkt = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "ota-start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, "ota-data");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 29, "ota-stop");

	/* OTA start command */
	if (!fu_telink_dfu_hid_device_ota_start(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* OTA firmware data */
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_TELINK_DFU_HID_DEVICE_START_ADDR,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_STRUCT_TELINK_DFU_HID_PKT_PAYLOAD_SIZE_OTA_DATA);
	if (!fu_telink_dfu_hid_device_write_blocks(self,
						   chunks,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* OTA stop command */
	if (!fu_telink_dfu_hid_device_ota_stop(self, fu_chunk_array_length(chunks), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;
	blob = fu_archive_lookup_by_fn(archive, "firmware.bin", error);
	if (blob == NULL)
		return FALSE;
	return fu_telink_dfu_hid_device_write_blob(self, blob, progress, error);
}

static void
fu_telink_dfu_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_telink_dfu_hid_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	g_auto(GStrv) ver_split = NULL;
	guint64 tmp = 0;

	/* version of supported Telink usb ota tool */
	self->windows_hid_tool_ver = 0;
	if (g_strcmp0(key, "TelinkHidToolVer") == 0) {
		ver_split = g_strsplit(value, ".", 2);
		if (!fu_strtoull(ver_split[0], &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->windows_hid_tool_ver += (guint16)tmp * 100;
		if (!fu_strtoull(ver_split[1], &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->windows_hid_tool_ver += (guint16)tmp;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_telink_dfu_hid_device_init(FuTelinkDfuHidDevice *self)
{
	fu_device_set_vendor(FU_DEVICE(self), "Telink");
	/* read the ReleaseNumber field of USB descriptor */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000); /* ms */
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_TELINK_DFU_ARCHIVE);
	fu_device_add_protocol(FU_DEVICE(self), "com.telink.dfu");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
}

static gboolean
fu_telink_dfu_hid_device_probe(FuDevice *device, GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	FuUsbDevice *usb_dev = FU_USB_DEVICE(FU_DEVICE(self));
	FuHidDevice *hid_dev = FU_HID_DEVICE(self);
	g_autoptr(GPtrArray) ifaces = NULL;

	ifaces = fu_usb_device_get_interfaces(usb_dev, error);
	if (ifaces == NULL)
		return FALSE;
	/* the last interface would always be reserved for OTA upgrade */
	fu_hid_device_set_interface(hid_dev, ifaces->len - 1);
	fu_hid_device_set_ep_addr_in(hid_dev, FU_TELINK_DFU_HID_EP_IN);
	fu_hid_device_set_ep_addr_out(hid_dev, FU_TELINK_DFU_HID_EP_OUT);

	/* FuHidDevice->probe */
	if (!FU_DEVICE_CLASS(fu_telink_dfu_hid_device_parent_class)->probe(device, error))
		return FALSE;

	return TRUE;
}

static void
fu_telink_dfu_hid_device_class_init(FuTelinkDfuHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_telink_dfu_hid_device_probe;
	device_class->write_firmware = fu_telink_dfu_hid_device_write_firmware;
	device_class->set_progress = fu_telink_dfu_hid_device_set_progress;
	device_class->set_quirk_kv = fu_telink_dfu_hid_device_set_quirk_kv;
	device_class->to_string = fu_telink_dfu_hid_device_to_string;
}
