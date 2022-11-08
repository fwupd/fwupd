/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-ti-tps6598x-common.h"
#include "fu-ti-tps6598x-device.h"
#include "fu-ti-tps6598x-firmware.h"
#include "fu-ti-tps6598x-pd-device.h"

struct _FuTiTps6598xDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuTiTps6598xDevice, fu_ti_tps6598x_device, FU_TYPE_USB_DEVICE)

#define TI_TPS6598X_DEVICE_USB_TIMEOUT 2000 /* ms */

/* command types in USB messages from PC to device */
#define TI_TPS6598X_USB_REQUEST_WRITE 0xFD
#define TI_TPS6598X_USB_REQUEST_READ  0xFE
#define TI_TPS6598X_USB_BUFFER_SIZE   8 /* bytes */

/* read @length bytes from address @addr */
static GByteArray *
fu_ti_tps6598x_device_usbep_read_raw(FuTiTps6598xDevice *self,
				     guint16 addr,
				     guint8 length,
				     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	gsize actual_length = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* first byte is length */
	fu_byte_array_set_size(buf, length + 1, 0x0);

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   TI_TPS6598X_USB_REQUEST_READ,
					   addr,
					   0x0, /* idx */
					   buf->data,
					   buf->len,
					   &actual_length,
					   TI_TPS6598X_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return NULL;
	}
	if (g_getenv("FWUPD_TI_TPS6598X_VERBOSE") != NULL) {
		g_autofree gchar *title = g_strdup_printf("read@0x%x", addr);
		fu_dump_raw(G_LOG_DOMAIN, title, buf->data, buf->len);
	}
	if (actual_length != buf->len) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "got 0x%x but requested 0x%x",
			    (guint)actual_length,
			    (guint)buf->len);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

/* read @length bytes from address @addr */
static GByteArray *
fu_ti_tps6598x_device_usbep_read(FuTiTps6598xDevice *self,
				 guint16 addr,
				 guint8 length,
				 GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* first byte is length */
	buf = fu_ti_tps6598x_device_usbep_read_raw(self, addr, length, error);
	if (buf == NULL)
		return NULL;

	/* check then remove size */
	if (buf->data[0] < length) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "response 0x%x but requested 0x%x",
			    (guint)buf->data[0],
			    (guint)length);
		return NULL;
	}
	g_byte_array_remove_index(buf, 0);

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_ti_tps6598x_device_usbep_write(FuTiTps6598xDevice *self,
				  guint16 addr,
				  GByteArray *buf,
				  GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GPtrArray) chunks = NULL;

	if (g_getenv("FWUPD_TI_TPS6598X_VERBOSE") != NULL) {
		g_autofree gchar *title = g_strdup_printf("write@0x%x", addr);
		fu_dump_raw(G_LOG_DOMAIN, title, buf->data, buf->len);
	}
	chunks =
	    fu_chunk_array_mutable_new(buf->data, buf->len, 0x0, 0x0, TI_TPS6598X_USB_BUFFER_SIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint16 idx = 0;
		gsize actual_length = 0;

		/* for the first chunk use the total data length */
		if (i == 0)
			idx = buf->len;
		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   TI_TPS6598X_USB_REQUEST_WRITE,
						   addr,
						   idx, /* idx */
						   fu_chunk_get_data_out(chk),
						   fu_chunk_get_data_sz(chk),
						   &actual_length,
						   TI_TPS6598X_DEVICE_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "failed to contact device: ");
			return FALSE;
		}
		if (actual_length != fu_chunk_get_data_sz(chk)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "wrote 0x%x but expected 0x%x",
				    (guint)actual_length,
				    (guint)fu_chunk_get_data_sz(chk));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

/* read the specified DATA register */
static GByteArray *
fu_ti_tps6598x_device_read_data(FuTiTps6598xDevice *self, gsize bufsz, GError **error)
{
	g_autoptr(GByteArray) buf =
	    fu_ti_tps6598x_device_usbep_read(self, TI_TPS6598X_REGISTER_DATA3, bufsz, error);
	if (buf == NULL) {
		g_prefix_error(error,
			       "failed to read data at 0x%x: ",
			       (guint)TI_TPS6598X_REGISTER_DATA3);
		return NULL;
	}
	return g_steal_pointer(&buf);
}

/* write to the DATA register */
static gboolean
fu_ti_tps6598x_device_write_data(FuTiTps6598xDevice *self, GByteArray *buf, GError **error)
{
	if (!fu_ti_tps6598x_device_usbep_write(self, TI_TPS6598X_REGISTER_DATA3, buf, error)) {
		g_prefix_error(error,
			       "failed to write data at 0x%x: ",
			       (guint)TI_TPS6598X_REGISTER_DATA3);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ti_tps6598x_device_write_4cc(FuTiTps6598xDevice *self,
				const gchar *cmd,
				GByteArray *data,
				GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* sanity check */
	if (strlen(cmd) != 4) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "expected 4-char cmd");
		return FALSE;
	}
	if (data != NULL) {
		if (!fu_ti_tps6598x_device_write_data(self, data, error))
			return FALSE;
	}
	for (guint i = 0; i < 4; i++)
		fu_byte_array_append_uint8(buf, cmd[i]);
	return fu_ti_tps6598x_device_usbep_write(self, TI_TPS6598X_REGISTER_CMD3, buf, error);
}

static gboolean
fu_ti_tps6598x_device_reset_hard(FuTiTps6598xDevice *self, GError **error)
{
	return fu_ti_tps6598x_device_write_4cc(self, "GAID", NULL, error);
}

static gboolean
fu_ti_tps6598x_device_wait_for_command_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuTiTps6598xDevice *self = FU_TI_TPS6598X_DEVICE(device);
	g_autoptr(GByteArray) buf = NULL;

	/* 4 bytes of data and the first byte is length */
	buf = fu_ti_tps6598x_device_usbep_read(self, TI_TPS6598X_REGISTER_CMD3, 4, error);
	if (buf == NULL)
		return FALSE;

	/* check the value of the cmd register */
	if (buf->data[0] != 0 || buf->data[1] != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_ARGUMENT,
			    "invalid status register, got 0x%02x:0x%02x",
			    buf->data[1],
			    buf->data[2]);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* wait for a 4CC command to complete, defaults for count is 15ms, delay 100ms */
static gboolean
fu_ti_tps6598x_device_wait_for_command(FuTiTps6598xDevice *self,
				       guint count,
				       guint delay,
				       GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_ti_tps6598x_device_wait_for_command_cb,
				    count,
				    delay,
				    NULL,
				    error);
}

static gboolean
fu_ti_tps6598x_device_target_reboot(FuTiTps6598xDevice *self, guint8 slaveNum, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	fu_byte_array_append_uint8(buf, slaveNum);
	fu_byte_array_append_uint8(buf, 0);
	if (!fu_ti_tps6598x_device_write_4cc(self, "DSRT", buf, error))
		return FALSE;
	return fu_ti_tps6598x_device_wait_for_command(self, 15, 100, error);
}

static gboolean
fu_ti_tps6598x_device_maybe_reboot(FuTiTps6598xDevice *self, GError **error)
{
	/* reset the targets first */
	if (!fu_ti_tps6598x_device_target_reboot(self, 0, error))
		return FALSE;
	if (!fu_ti_tps6598x_device_target_reboot(self, 1, error))
		return FALSE;
	return fu_ti_tps6598x_device_reset_hard(self, error);
}

/* prepare device to receive the upcoming data transactions */
static gboolean
fu_ti_tps6598x_device_sfwi(FuTiTps6598xDevice *self, GError **error)
{
	guint8 res;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_ti_tps6598x_device_write_4cc(self, "SFWi", NULL, error))
		return FALSE;
	if (!fu_ti_tps6598x_device_wait_for_command(self, 15, 100, error))
		return FALSE;
	buf = fu_ti_tps6598x_device_read_data(self, 6, error);
	if (buf == NULL)
		return FALSE;
	res = buf->data[0] & 0b1111;
	if (res != TI_TPS6598X_SFWI_SUCCESS) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_ARGUMENT,
			    "SFWi failed, got %s [0x%02x]",
			    fu_ti_tps6598x_device_sfwi_strerror(res),
			    res);
		return FALSE;
	}

	/* success */
	if (g_getenv("FWUPD_TI_TPS6598X_VERBOSE") != NULL) {
		g_debug("prod-key-present: %u", (guint)(buf->data[2] & 0b00010) >> 1);
		g_debug("engr-key-present: %u", (guint)(buf->data[2] & 0b00100) >> 2);
		g_debug("new-flash-region: %u", (guint)(buf->data[2] & 0b11000) >> 3);
	}
	return TRUE;
}

/* provide device with the next 64 bytes to be flashed into the SPI */
static gboolean
fu_ti_tps6598x_device_sfwd(FuTiTps6598xDevice *self, GByteArray *data, GError **error)
{
	guint8 res;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_ti_tps6598x_device_write_4cc(self, "SFWd", data, error))
		return FALSE;
	if (!fu_ti_tps6598x_device_wait_for_command(self, 15, 100, error))
		return FALSE;
	buf = fu_ti_tps6598x_device_read_data(self, 1, error);
	if (buf == NULL)
		return FALSE;
	res = buf->data[0] & 0b1111;
	if (res != TI_TPS6598X_SFWD_SUCCESS) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_ARGUMENT,
			    "SFWd failed, got %s [0x%02x]",
			    fu_ti_tps6598x_device_sfwd_strerror(res),
			    res);
		return FALSE;
	}

	/* success */
	if (g_getenv("FWUPD_TI_TPS6598X_VERBOSE") != NULL)
		g_debug("more-data-expected: %i", (buf->data[0] & 0x80) > 0);
	return TRUE;
}

/* pass the image signature information to device for verification of the data */
static gboolean
fu_ti_tps6598x_device_sfws(FuTiTps6598xDevice *self, GByteArray *data, GError **error)
{
	guint8 res;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_ti_tps6598x_device_write_4cc(self, "SFWs", data, error))
		return FALSE;
	if (!fu_ti_tps6598x_device_wait_for_command(self, 300, 1000, error))
		return FALSE;
	buf = fu_ti_tps6598x_device_read_data(self, 10, error);
	if (buf == NULL)
		return FALSE;
	res = buf->data[0] & 0b1111;
	if (res != TI_TPS6598X_SFWS_SUCCESS) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_ARGUMENT,
			    "SFWs failed, got %s [0x%02x]",
			    fu_ti_tps6598x_device_sfws_strerror(res),
			    res);
		return FALSE;
	}

	/* success */
	if (g_getenv("FWUPD_TI_TPS6598X_VERBOSE") != NULL) {
		g_debug("more-data-expected: %i", (buf->data[0] & 0x80) > 0);
		g_debug("signature-data-block: %u", (guint)buf->data[1]);
		g_debug("prod-key-present: %u", (guint)(buf->data[2] & 0b00010) >> 1);
		g_debug("engr-key-present: %u", (guint)(buf->data[2] & 0b00100) >> 2);
		g_debug("new-flash-region: %u", (guint)(buf->data[2] & 0b11000) >> 3);
		g_debug("hash-match: %u", (guint)(buf->data[2] & 0b1100000) >> 5);
	}
	return TRUE;
}

GByteArray *
fu_ti_tps6598x_device_read_target_register(FuTiTps6598xDevice *self,
					   guint8 target,
					   guint8 addr,
					   guint8 length,
					   GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GByteArray) data = g_byte_array_new();

	fu_byte_array_append_uint8(data, target);
	fu_byte_array_append_uint8(data, addr);
	fu_byte_array_append_uint8(data, length);
	if (!fu_ti_tps6598x_device_write_4cc(self, "DSRD", data, error))
		return NULL;
	if (!fu_ti_tps6598x_device_wait_for_command(self, 300, 1000, error))
		return NULL;
	buf = fu_ti_tps6598x_device_read_data(self, length + 1, error);
	if (buf == NULL)
		return NULL;

	/* check then remove response code */
	if (buf->data[0] != 0x00) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "response code 0x%02x",
			    (guint)buf->data[0]);
		return NULL;
	}
	g_byte_array_remove_index(buf, 0);

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_ti_tps6598x_device_ensure_version(FuTiTps6598xDevice *self, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) buf = NULL;

	/* get bcdVersion */
	buf = fu_ti_tps6598x_device_usbep_read(self, TI_TPS6598X_REGISTER_VERSION, 4, error);
	if (buf == NULL)
		return FALSE;
	str = g_strdup_printf("%X.%X.%X", buf->data[2], buf->data[1], buf->data[0]);
	fu_device_set_version(FU_DEVICE(self), str);
	return TRUE;
}

static gboolean
fu_ti_tps6598x_device_ensure_mode(FuTiTps6598xDevice *self, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_ti_tps6598x_device_usbep_read(self, TI_TPS6598X_REGISTER_MODE, 4, error);
	if (buf == NULL)
		return FALSE;

	/* ensure in recognized mode */
	str = fu_strsafe((const gchar *)buf->data, buf->len);
	if (g_strcmp0(str, "APP ") == 0) {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}
	if (g_strcmp0(str, "BOOT") == 0) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}

	/* unhandled */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_ARGUMENT,
		    "device in unknown mode: %s",
		    str);
	return FALSE;
}

static gboolean
fu_ti_tps6598x_device_ensure_uid(FuTiTps6598xDevice *self, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_ti_tps6598x_device_usbep_read(self, TI_TPS6598X_REGISTER_UID, 16, error);
	if (buf == NULL)
		return FALSE;
	str = fu_byte_array_to_string(buf);
	fu_device_add_instance_str(FU_DEVICE(self), "UID", str);
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "USB",
					   "VID",
					   "PID",
					   "UID",
					   NULL);
}

static gboolean
fu_ti_tps6598x_device_ensure_ouid(FuTiTps6598xDevice *self, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_ti_tps6598x_device_usbep_read(self, TI_TPS6598X_REGISTER_OUID, 8, error);
	if (buf == NULL)
		return FALSE;
	str = fu_byte_array_to_string(buf);
	fu_device_add_instance_str(FU_DEVICE(self), "OUID", str);
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "USB",
					   "VID",
					   "PID",
					   "OUID",
					   NULL);
}

static gboolean
fu_ti_tps6598x_device_ensure_config(FuTiTps6598xDevice *self, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_ti_tps6598x_device_usbep_read(self, TI_TPS6598X_REGISTER_OTP_CONFIG, 12, error);
	if (buf == NULL)
		return FALSE;
	str = fu_byte_array_to_string(buf);
	fu_device_add_instance_strup(FU_DEVICE(self), "CONFIG", str);
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "USB",
					   "VID",
					   "PID",
					   "CONFIG",
					   NULL);
}

static gboolean
fu_ti_tps6598x_device_setup(FuDevice *device, GError **error)
{
	FuTiTps6598xDevice *self = FU_TI_TPS6598X_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ti_tps6598x_device_parent_class)->setup(device, error))
		return FALSE;

	/* there are two devices with the same VID:PID -- ignore the non-vendor one */
	if (g_usb_device_get_device_class(usb_device) != G_USB_DEVICE_CLASS_VENDOR_SPECIFIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "non-vendor specific interface ignored");
		return FALSE;
	}

	/* get hardware details */
	if (!fu_ti_tps6598x_device_ensure_version(self, error)) {
		g_prefix_error(error, "failed to read version: ");
		return FALSE;
	}
	if (!fu_ti_tps6598x_device_ensure_mode(self, error)) {
		g_prefix_error(error, "failed to read mode: ");
		return FALSE;
	}
	if (!fu_ti_tps6598x_device_ensure_uid(self, error)) {
		g_prefix_error(error, "failed to read UID: ");
		return FALSE;
	}
	if (!fu_ti_tps6598x_device_ensure_ouid(self, error)) {
		g_prefix_error(error, "failed to read oUID: ");
		return FALSE;
	}
	if (!fu_ti_tps6598x_device_ensure_config(self, error)) {
		g_prefix_error(error, "failed to read OTP config: ");
		return FALSE;
	}

	/* create child PD devices */
	for (guint i = 0; i < FU_TI_TPS6598X_PD_MAX; i++) {
		g_autoptr(FuDevice) device_pd =
		    fu_ti_tps6598x_pd_device_new(fu_device_get_context(device), i);
		fu_device_add_child(device, device_pd);
	}

	/* success */
	return TRUE;
}

static void
fu_ti_tps6598x_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	FuTiTps6598xDevice *self = FU_TI_TPS6598X_DEVICE(device);
	for (guint i = 0; i < 0xFF; i++) {
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(GError) error_local = NULL;

		buf = fu_ti_tps6598x_device_usbep_read_raw(self, i, 62, &error_local);
		if (buf == NULL) {
			g_debug("failed to get DMC register 0x%02x: %s", i, error_local->message);
			continue;
		}
		if (!fu_ti_tps6598x_byte_array_is_nonzero(buf))
			continue;
		g_hash_table_insert(metadata,
				    g_strdup_printf("Tps6598xDmcRegister@0x%02x", i),
				    fu_byte_array_to_string(buf));
	}
}

static gboolean
fu_ti_tps6598x_device_write_chunks(FuTiTps6598xDevice *self,
				   GPtrArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) buf = g_byte_array_new();

		/* align */
		g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_64, 0xFF);
		if (!fu_ti_tps6598x_device_sfwd(self, buf, error)) {
			g_prefix_error(error, "failed to write chunk %u: ", i);
			return FALSE;
		}

		/* update progress */
		g_usleep(100 * 1000);
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ti_tps6598x_device_write_sfws_chunks(FuTiTps6598xDevice *self,
					GPtrArray *chunks,
					FuProgress *progress,
					GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) buf = g_byte_array_new();

		/* align and pad low before sending */
		g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_64, 0x0);
		if (!fu_ti_tps6598x_device_sfws(self, buf, error)) {
			g_prefix_error(error, "failed to write chunk %u: ", i);
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ti_tps6598x_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuTiTps6598xDevice *self = FU_TI_TPS6598X_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* hopefully this fails because the hardware rebooted */
	if (!fu_ti_tps6598x_device_maybe_reboot(self, &error_local)) {
		if (!g_error_matches(error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_NO_DEVICE)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring expected failure: %s", error_local->message);
	}

	/* success! */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

gboolean
fu_ti_tps6598x_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuTiTps6598xDevice *self = FU_TI_TPS6598X_DEVICE(device);
	g_autoptr(GBytes) fw_sig = NULL;
	g_autoptr(GBytes) fw_pubkey = NULL;
	g_autoptr(GBytes) fw_payload = NULL;
	g_autoptr(GPtrArray) chunks_pubkey = NULL;
	g_autoptr(GPtrArray) chunks_sig = NULL;
	g_autoptr(GPtrArray) chunks_payload = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 91, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 7, NULL);

	/* get payload image */
	fw_payload = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (fw_payload == NULL)
		return FALSE;

	/* SFWi */
	if (!fu_ti_tps6598x_device_sfwi(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each SFWd block */
	chunks_payload = fu_chunk_array_new_from_bytes(fw_payload,
						       0x0, /* start_addr */
						       0x0, /* page_sz */
						       64);
	if (!fu_ti_tps6598x_device_write_chunks(self,
						chunks_payload,
						fu_progress_get_child(progress),
						error)) {
		g_prefix_error(error, "failed to write SFWd: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* SFWs with signature */
	fw_sig = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_SIGNATURE, error);
	if (fw_sig == NULL)
		return FALSE;
	chunks_sig = fu_chunk_array_new_from_bytes(fw_sig,
						   0x0, /* start_addr */
						   0x0, /* page_sz */
						   64);
	if (!fu_ti_tps6598x_device_write_sfws_chunks(self,
						     chunks_sig,
						     fu_progress_get_child(progress),
						     error)) {
		g_prefix_error(error, "failed to write SFWs with signature: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* SFWs with pubkey */
	fw_pubkey = fu_firmware_get_image_by_id_bytes(firmware, "pubkey", error);
	if (fw_pubkey == NULL)
		return FALSE;
	chunks_pubkey = fu_chunk_array_new_from_bytes(fw_pubkey,
						      0x0, /* start_addr */
						      0x0, /* page_sz */
						      64);
	if (!fu_ti_tps6598x_device_write_sfws_chunks(self,
						     chunks_pubkey,
						     fu_progress_get_child(progress),
						     error)) {
		g_prefix_error(error, "failed to write SFWs with pubkey: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_ti_tps6598x_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 91, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 9, "reload");
}

static void
fu_ti_tps6598x_device_init(FuTiTps6598xDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.ti.tps6598x");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_TI_TPS6598X_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 30000);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x0);
}

static void
fu_ti_tps6598x_device_class_init(FuTiTps6598xDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_ti_tps6598x_device_write_firmware;
	klass_device->attach = fu_ti_tps6598x_device_attach;
	klass_device->setup = fu_ti_tps6598x_device_setup;
	klass_device->report_metadata_pre = fu_ti_tps6598x_device_report_metadata_pre;
	klass_device->set_progress = fu_ti_tps6598x_device_set_progress;
}
