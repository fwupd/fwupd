/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-touch-firmware.h"
#include "fu-focal-touch-hid-device.h"
#include "fu-focal-touch-struct.h"

struct _FuFocalTouchHidDevice {
	FuHidrawDevice parent_instance;
	guint16 verify_id;
};

G_DEFINE_TYPE(FuFocalTouchHidDevice, fu_focal_touch_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define REPORT_SIZE	    64
#define MAX_USB_PACKET_SIZE 56

static void
fu_focal_touch_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "VerifyId", self->verify_id);
}

static gboolean
fu_focal_touch_hid_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));

	/* check is valid - handle NULL subsystem */
	if (subsystem == NULL || g_strcmp0(subsystem, "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device has incorrect subsystem=%s, expected hidraw",
			    subsystem != NULL ? subsystem : "(null)");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static guint8
fu_focal_touch_hid_device_generate_checksum(const guint8 *buf, gsize bufsz)
{
	guint8 checksum = 0;
	for (gsize i = 0; i < bufsz; i++)
		checksum ^= buf[i];
	checksum++;
	return checksum;
}

static gboolean
fu_focal_touch_hid_device_send(FuFocalTouchHidDevice *self, GByteArray *buf, GError **error)
{
	guint buflen = buf->len;
	fu_byte_array_set_size(buf, REPORT_SIZE, 0x00);
	buf->data[buflen] = fu_focal_touch_hid_device_generate_checksum(buf->data + 1, buflen - 1);
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    buf->data,
					    buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static GByteArray *
fu_focal_touch_hid_device_recv_raw(FuFocalTouchHidDevice *self, GError **error)
{
	guint8 csum = 0;
	guint8 csum_actual;
	guint8 csum_offset = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_append_uint8(buf, 0x06);
	fu_byte_array_set_size(buf, REPORT_SIZE, 0x00);
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf->data,
					  buf->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return NULL;

	/* check checksum */
	if (!fu_memread_uint8_safe(buf->data, buf->len, 3, &csum_offset, error))
		return NULL;
	if (!fu_memread_uint8_safe(buf->data, buf->len, csum_offset, &csum, error))
		return NULL;
	csum_actual = fu_focal_touch_hid_device_generate_checksum(buf->data + 1, csum_offset - 1);
	if (csum != csum_actual) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got checksum 0x%02x, expected 0x%02x",
			    csum,
			    csum_actual);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_focal_touch_hid_device_read_reg_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	guint8 *val = (guint8 *)user_data;
	g_autoptr(FuStructFocalTouchReadRegisterRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv ReadRegister: ");
		return FALSE;
	}
	st_res = fu_struct_focal_touch_read_register_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	*val = fu_struct_focal_touch_read_register_res_get_value(st_res);
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_read_reg(FuFocalTouchHidDevice *self,
				   FuFocalTouchRegister reg_address,
				   guint8 *val, /* out */
				   GError **error)
{
	g_autoptr(FuStructFocalTouchReadRegisterReq) st_req =
	    fu_struct_focal_touch_read_register_req_new();

	/* write */
	fu_struct_focal_touch_read_register_req_set_address(st_req, reg_address);
	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error))
		return FALSE;

	/* read */
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focal_touch_hid_device_read_reg_cb,
				    5,
				    1 /* ms */,
				    val,
				    error);
}

static gboolean
fu_focal_touch_hid_device_write_bin_length(FuFocalTouchHidDevice *self,
					   gsize firmware_size,
					   GError **error)
{
	g_autoptr(FuStructFocalTouchBinLengthReq) st_req =
	    fu_struct_focal_touch_bin_length_req_new();
	g_autoptr(FuStructFocalTouchBinLengthRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_focal_touch_bin_length_req_set_size(st_req, firmware_size);
	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send BinLength: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv BinLength: ");
		return FALSE;
	}
	st_res = fu_struct_focal_touch_bin_length_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_enter_upgrade_mode(FuFocalTouchHidDevice *self, GError **error)
{
	g_autoptr(FuStructFocalTouchEnterUpgradeModeReq) st_req =
	    fu_struct_focal_touch_enter_upgrade_mode_req_new();
	g_autoptr(FuStructFocalTouchEnterUpgradeModeRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send EnterUpgradeMode: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv EnterUpgradeMode: ");
		return FALSE;
	}
	st_res =
	    fu_struct_focal_touch_enter_upgrade_mode_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_check_current_state(FuFocalTouchHidDevice *self,
					      FuFocalTouchUcMode *val,
					      GError **error)
{
	g_autoptr(FuStructFocalTouchCheckCurrentStateReq) st_req =
	    fu_struct_focal_touch_check_current_state_req_new();
	g_autoptr(FuStructFocalTouchCheckCurrentStateRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send CheckCurrentState: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv CheckCurrentState: ");
		return FALSE;
	}
	st_res =
	    fu_struct_focal_touch_check_current_state_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	*val = fu_struct_focal_touch_check_current_state_res_get_mode(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_wait_for_upgrade_ready_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	g_autoptr(FuStructFocalTouchReadyForUpgradeReq) st_req =
	    fu_struct_focal_touch_ready_for_upgrade_req_new();
	g_autoptr(FuStructFocalTouchReadyForUpgradeRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send ReadyForUpgrade: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv ReadyForUpgrade: ");
		return FALSE;
	}
	st_res = fu_struct_focal_touch_ready_for_upgrade_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* check Status Byte */
	if (fu_struct_focal_touch_ready_for_upgrade_res_get_status(st_res) != 0x02) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "device busy, status 0x%02x",
			    fu_struct_focal_touch_ready_for_upgrade_res_get_status(st_res));
		return FALSE;
	}

	return TRUE;
}

/* wait for ready */
static gboolean
fu_focal_touch_hid_device_wait_for_upgrade_ready(FuFocalTouchHidDevice *self,
						 guint retries,
						 GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focal_touch_hid_device_wait_for_upgrade_ready_cb,
				    retries,
				    1,
				    NULL,
				    error);
}

static gboolean
fu_focal_touch_hid_device_read_update_id_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	guint16 *upgrade_id = (guint16 *)user_data;
	g_autoptr(FuStructFocalTouchUsbReadUpgradeIdReq) st_req =
	    fu_struct_focal_touch_usb_read_upgrade_id_req_new();
	g_autoptr(FuStructFocalTouchUsbReadUpgradeIdRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send UsbReadUpgradeId: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv UsbReadUpgradeId: ");
		return FALSE;
	}
	st_res =
	    fu_struct_focal_touch_usb_read_upgrade_id_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	*upgrade_id = fu_struct_focal_touch_usb_read_upgrade_id_res_get_upgrade_id(st_res);

	/* success */
	return TRUE;
}

/* get bootload id */
static gboolean
fu_focal_touch_hid_device_read_update_id(FuFocalTouchHidDevice *self,
					 guint16 *upgrade_id,
					 GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focal_touch_hid_device_read_update_id_cb,
				    10,
				    1 /* ms */,
				    upgrade_id,
				    error);
}

/* erase flash */
static gboolean
fu_focal_touch_hid_device_erase_flash(FuFocalTouchHidDevice *self, GError **error)
{
	g_autoptr(FuStructFocalTouchUsbEraseFlashReq) st_req =
	    fu_struct_focal_touch_usb_erase_flash_req_new();
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send UsbEraseFlash: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv UsbEraseFlash: ");
		return FALSE;
	}

	/* check was correct response */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_send_data_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv SendData: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* send write data */
static gboolean
fu_focal_touch_hid_device_send_data(FuFocalTouchHidDevice *self,
				    FuFocalTouchPacketType packet_type,
				    const guint8 *buf,
				    guint8 bufsz,
				    GError **error)
{
	g_autoptr(FuStructFocalTouchSendDataReq) st_req = fu_struct_focal_touch_send_data_req_new();

	fu_struct_focal_touch_send_data_req_set_packet_type(st_req, packet_type);
	g_byte_array_append(st_req->buf, buf, bufsz);
	fu_struct_focal_touch_send_data_req_set_len(st_req, st_req->buf->len);
	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 2);
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focal_touch_hid_device_send_data_cb,
				    4,
				    1 /* ms */,
				    NULL,
				    error);
}

/* get checksum for write done */
static gboolean
fu_focal_touch_hid_device_checksum_upgrade(FuFocalTouchHidDevice *self,
					   guint32 *val,
					   GError **error)
{
	g_autoptr(FuStructFocalTouchUpgradeChecksumReq) st_req =
	    fu_struct_focal_touch_upgrade_checksum_req_new();
	g_autoptr(FuStructFocalTouchUpgradeChecksumRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send UpgradeChecksum: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv UpgradeChecksum: ");
		return FALSE;
	}

	/* success */
	st_res = fu_struct_focal_touch_upgrade_checksum_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	*val = fu_struct_focal_touch_upgrade_checksum_res_get_value(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_setup(FuDevice *device, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	guint8 buf[2] = {0x0};

	if (self->verify_id == 0x5822)
		fu_device_set_firmware_size(device, 0x1E000);

	/* get current firmware version */
	if (!fu_focal_touch_hid_device_read_reg(self,
						FU_FOCAL_TOUCH_REGISTER_FW_VERSION1,
						buf,
						error)) {
		g_prefix_error_literal(error, "failed to read version1: ");
		return FALSE;
	}
	if (!fu_focal_touch_hid_device_read_reg(self,
						FU_FOCAL_TOUCH_REGISTER_FW_VERSION2,
						buf + 1,
						error)) {
		g_prefix_error_literal(error, "failed to read version2: ");
		return FALSE;
	}
	fu_device_set_version_raw(device, fu_memread_uint16(buf, G_BIG_ENDIAN));

	/* success */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_write_chunks(FuFocalTouchHidDevice *self,
				       FuChunkArray *chunks,
				       FuProgress *progress,
				       GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		FuFocalTouchPacketType packet_type = FU_FOCAL_TOUCH_PACKET_TYPE_MID;
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (i == 0)
			packet_type = FU_FOCAL_TOUCH_PACKET_TYPE_FIRST;
		else if (i == fu_chunk_array_length(chunks) - 1)
			packet_type = FU_FOCAL_TOUCH_PACKET_TYPE_END;

		if (!fu_focal_touch_hid_device_send_data(self,
							 packet_type,
							 fu_chunk_get_data(chk),
							 fu_chunk_get_data_sz(chk),
							 error)) {
			g_prefix_error(error, "failed to write chunk %u: ", i);
			return FALSE;
		}
		if (!fu_focal_touch_hid_device_wait_for_upgrade_ready(self, 20, error)) {
			g_prefix_error(error, "failed to wait for chunk %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	guint16 upgrade_id_tmp = 0;
	guint32 checksum = 0;
	guint32 upgrade_id = 0;
	guint calculate_checksum_delay = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	if (self->verify_id == 0x5822) {
		/* FT3637 */
		upgrade_id = 0x582E;
		calculate_checksum_delay = 50; /* ms */
	} else if (self->verify_id == 0x5456) {
		/* FT3437u */
		upgrade_id = 0x542C;
		calculate_checksum_delay = 200; /* ms */
	} else if (self->verify_id == 0x3C83) {
		/* FT3C83 */
		upgrade_id = 0x3CA3;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot write firmware, unknown VerifyId pair (ID: 0x%04x)",
			    self->verify_id);
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "reset");

	/* simple image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* check chip id and erase flash */
	if (!fu_focal_touch_hid_device_wait_for_upgrade_ready(self, 6, error))
		return FALSE;
	if (!fu_focal_touch_hid_device_read_update_id(self, &upgrade_id_tmp, error))
		return FALSE;
	if (upgrade_id_tmp != upgrade_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got upgrade_id_tmp 0x%04x, expected 0x%04x",
			    upgrade_id_tmp,
			    (guint)upgrade_id);
		return FALSE;
	}
	if (self->verify_id == 0x3C83) {
		gsize streamsz = fu_firmware_get_size(firmware);
		if (!fu_focal_touch_hid_device_write_bin_length(self,
								(streamsz + 3) / 4 * 4,
								error))
			return FALSE;
	}
	if (!fu_focal_touch_hid_device_erase_flash(self, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 1000);
	if (!fu_focal_touch_hid_device_wait_for_upgrade_ready(self, 20, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send packet data */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						MAX_USB_PACKET_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_focal_touch_hid_device_write_chunks(self,
						    chunks,
						    fu_progress_get_child(progress),
						    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write flash end and check ready (calculate checksum) */
	fu_device_sleep(FU_DEVICE(self), calculate_checksum_delay);
	if (!fu_focal_touch_hid_device_wait_for_upgrade_ready(self, 5, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify checksum */
	if (!fu_focal_touch_hid_device_checksum_upgrade(self, &checksum, error))
		return FALSE;
	if (checksum != fu_focal_touch_firmware_get_checksum(FU_FOCAL_TOUCH_FIRMWARE(firmware))) {
		fu_device_sleep(FU_DEVICE(self), 500);
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "device checksum invalid, got 0x%08x, expected 0x%08x",
		    checksum,
		    fu_focal_touch_firmware_get_checksum(FU_FOCAL_TOUCH_FIRMWARE(firmware)));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

/* called after attach, but only when the firmware has been updated */
static gboolean
fu_focal_touch_hid_device_reload(FuDevice *device, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	guint8 buf[2] = {0x0};
	guint16 verify_id;

	fu_device_sleep(device, 500);
	if (!fu_focal_touch_hid_device_read_reg(self,
						FU_FOCAL_TOUCH_REGISTER_VERIFY_ID1,
						&buf[0],
						error))
		return FALSE;
	if (!fu_focal_touch_hid_device_read_reg(self,
						FU_FOCAL_TOUCH_REGISTER_VERIFY_ID2,
						&buf[1],
						error))
		return FALSE;
	verify_id = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (verify_id != self->verify_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware id invalid, got 0x%04x and expected 0x%04x",
			    verify_id,
			    self->verify_id);
		return FALSE;
	}
	return fu_focal_touch_hid_device_setup(device, error);
}

static gboolean
fu_focal_touch_hid_device_detach_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	FuFocalTouchUcMode uc_mode = 0;

	if (!fu_focal_touch_hid_device_enter_upgrade_mode(self, error)) {
		g_prefix_error_literal(error, "failed to enter upgrade mode: ");
		return FALSE;
	}

	/* get current state */
	if (!fu_focal_touch_hid_device_check_current_state(self, &uc_mode, error))
		return FALSE;
	if (uc_mode != FU_FOCAL_TOUCH_UC_MODE_UPGRADE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got uc_mode 0x%02x, expected 0x%02x",
			    uc_mode,
			    (guint)1);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	g_autoptr(FuStructFocalTouchEnterUpgradeModeReq) st_req =
	    fu_struct_focal_touch_enter_upgrade_mode_req_new();
	g_autoptr(GByteArray) buf = NULL;

	/* command to go from APP --> Bootloader */
	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send EnterUpgradeMode: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv EnterUpgradeMode: ");
		return FALSE;
	}
	fu_device_sleep(device, 200);

	/* second command : bootloader normal mode --> bootloader upgrade mode */
	if (!fu_device_retry_full(device,
				  fu_focal_touch_hid_device_detach_cb,
				  3,
				  200 /* ms */,
				  progress,
				  error))
		return FALSE;

	/* success */
	fu_device_sleep(device, 200);
	return TRUE;
}

static gboolean
fu_focal_touch_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);
	g_autoptr(FuStructFocalTouchExitUpgradeModeReq) st_req =
	    fu_struct_focal_touch_exit_upgrade_mode_req_new();
	g_autoptr(FuStructFocalTouchExitUpgradeModeRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_focal_touch_hid_device_send(self, st_req->buf, error)) {
		g_prefix_error_literal(error, "failed to send ExitUpgradeMode: ");
		return FALSE;
	}
	buf = fu_focal_touch_hid_device_recv_raw(self, error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to recv ExitUpgradeMode: ");
		return FALSE;
	}
	st_res = fu_struct_focal_touch_exit_upgrade_mode_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	fu_device_sleep(device, 500);
	return TRUE;
}

static void
fu_focal_touch_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_focal_touch_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_focal_touch_hid_device_set_quirk_kv(FuDevice *device,
				       const gchar *key,
				       const gchar *value,
				       GError **error)
{
	FuFocalTouchHidDevice *self = FU_FOCAL_TOUCH_HID_DEVICE(device);

	/* optional */
	if (g_strcmp0(key, "FocalTouchVerifyId") == 0) {
		guint64 value64 = 0;
		if (!fu_strtoull(value, &value64, 0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		self->verify_id = (guint16)value64;
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
fu_focal_touch_hid_device_init(FuFocalTouchHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_FOCAL_TOUCH_FIRMWARE);
	fu_device_set_summary(FU_DEVICE(self), "Touch Device");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.focal.tp");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_focal_touch_hid_device_class_init(FuFocalTouchHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_focal_touch_hid_device_to_string;
	device_class->attach = fu_focal_touch_hid_device_attach;
	device_class->detach = fu_focal_touch_hid_device_detach;
	device_class->setup = fu_focal_touch_hid_device_setup;
	device_class->reload = fu_focal_touch_hid_device_reload;
	device_class->write_firmware = fu_focal_touch_hid_device_write_firmware;
	device_class->probe = fu_focal_touch_hid_device_probe;
	device_class->set_progress = fu_focal_touch_hid_device_set_progress;
	device_class->convert_version = fu_focal_touch_hid_device_convert_version;
	device_class->set_quirk_kv = fu_focal_touch_hid_device_set_quirk_kv;
}
