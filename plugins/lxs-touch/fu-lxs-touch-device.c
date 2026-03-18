/*
 * Copyright 2026 JS Park <mameforever2@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lxs-touch-device.h"
#include "fu-lxs-touch-firmware.h"
#include "fu-lxs-touch-struct.h"

struct _FuLxsTouchDevice {
	FuHidrawDevice parent_instance;
	gboolean use_4k_mode;
	guint8 x_size;
	guint8 y_size;
};

G_DEFINE_TYPE(FuLxsTouchDevice, fu_lxs_touch_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_LXS_TOUCH_BUFFER_SIZE 64

#define FU_LXS_TOUCH_PROTOCOL_NAME_SWIP "SWIP"
#define FU_LXS_TOUCH_PROTOCOL_NAME_DFUP "DFUP"

#define FU_LXS_TOUCH_DOWNLOAD_CHUNK_SIZE_NORMAL 128
#define FU_LXS_TOUCH_DOWNLOAD_CHUNK_SIZE_4K	4096
#define FU_LXS_TOUCH_TRANSMIT_UNIT_NORMAL	16
#define FU_LXS_TOUCH_TRANSMIT_UNIT_4K		48

static void
fu_lxs_touch_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	fwupd_codec_string_append_bool(str, idt, "Use4kMode", self->use_4k_mode);
	fwupd_codec_string_append_int(str, idt, "XSize", self->x_size);
	fwupd_codec_string_append_int(str, idt, "YSize", self->y_size);
}

static gboolean
fu_lxs_touch_device_read(FuLxsTouchDevice *self, guint16 command, guint16 datasz, GError **error)
{
	g_autoptr(FuStructLxsTouchPacket) st = fu_struct_lxs_touch_packet_new();

	fu_struct_lxs_touch_packet_set_flag(st, FU_LXS_TOUCH_FLAG_READ);
	fu_struct_lxs_touch_packet_set_length(st, datasz);
	fu_struct_lxs_touch_packet_set_command(st, command);
	fu_byte_array_set_size(st->buf, FU_LXS_TOUCH_BUFFER_SIZE, 0x0);
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   st->buf->data,
					   st->buf->len,
					   FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					   error);
}

static gboolean
fu_lxs_touch_device_write(FuLxsTouchDevice *self,
			  guint16 command,
			  const guint8 *data,
			  guint16 datasz,
			  GError **error)
{
	g_autoptr(FuStructLxsTouchPacket) st = fu_struct_lxs_touch_packet_new();

	/* add data */
	fu_struct_lxs_touch_packet_set_flag(st, FU_LXS_TOUCH_FLAG_WRITE);
	fu_struct_lxs_touch_packet_set_length(st, datasz + 2);
	fu_struct_lxs_touch_packet_set_command(st, command);
	if (data != NULL)
		g_byte_array_append(st->buf, data, datasz);
	fu_byte_array_set_size(st->buf, FU_LXS_TOUCH_BUFFER_SIZE, 0x0);
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   st->buf->data,
					   st->buf->len,
					   FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					   error);
}

static gboolean
fu_lxs_touch_device_read_data(FuLxsTouchDevice *self,
			      guint16 command,
			      guint8 *data,
			      gsize datasz,
			      GError **error)
{
	guint8 buf[FU_LXS_TOUCH_BUFFER_SIZE] = {0};

	/* 0x68 then 0x69 */
	if (!fu_lxs_touch_device_write(self, command, NULL, 0, error))
		return FALSE;
	if (!fu_lxs_touch_device_read(self, command, datasz, error))
		return FALSE;
	if (!fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self),
					 buf,
					 FU_LXS_TOUCH_BUFFER_SIZE,
					 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					 error))
		return FALSE;
	if (data != NULL) {
		if (!fu_memcpy_safe(data, datasz, 0x0, buf, sizeof(buf), 0x04, datasz, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_wait_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	guint8 buf[8] = {0};
	g_autoptr(FuStructLxsTouchGetter) st = NULL;

	if (!fu_lxs_touch_device_read_data(self,
					   FU_LXS_TOUCH_REG_CTRL_GETTER,
					   buf,
					   sizeof(buf),
					   error))
		return FALSE;
	st = fu_struct_lxs_touch_getter_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_lxs_touch_getter_get_ready_status(st) != FU_LXS_TOUCH_READY_STATUS_READY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "device not ready");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_wait_ready(FuLxsTouchDevice *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_lxs_touch_device_wait_ready_cb,
				    5000,
				    1,
				    NULL,
				    error);
}

static gboolean
fu_lxs_touch_device_ensure_version(FuLxsTouchDevice *self, GError **error)
{
	guint8 buf[8] = {0};
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructLxsTouchVersion) st_ver = NULL;

	if (!fu_lxs_touch_device_read_data(self,
					   FU_LXS_TOUCH_REG_INFO_VERSION,
					   buf,
					   sizeof(buf),
					   error))
		return FALSE;
	st_ver = fu_struct_lxs_touch_version_parse(buf, sizeof(buf), 0x0, error);
	if (st_ver == NULL)
		return FALSE;
	version = g_strdup_printf("%04X.%04X.%04X.%04X",
				  fu_struct_lxs_touch_version_get_boot_ver(st_ver),
				  fu_struct_lxs_touch_version_get_core_ver(st_ver),
				  fu_struct_lxs_touch_version_get_app_ver(st_ver),
				  fu_struct_lxs_touch_version_get_param_ver(st_ver));
	fu_device_set_version(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_ensure_mode(FuLxsTouchDevice *self, GError **error)
{
	g_autoptr(FuStructLxsTouchInterface) st = NULL;
	g_autofree gchar *protocol_name = NULL;
	guint8 buf[8] = {0};

	if (!fu_lxs_touch_device_read_data(self,
					   FU_LXS_TOUCH_REG_INFO_INTERFACE,
					   buf,
					   sizeof(buf),
					   error))
		return FALSE;

	st = fu_struct_lxs_touch_interface_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;

	protocol_name = fu_struct_lxs_touch_interface_get_protocol_name(st);
	if (g_str_has_prefix(protocol_name, FU_LXS_TOUCH_PROTOCOL_NAME_DFUP)) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else if (g_str_has_prefix(protocol_name, FU_LXS_TOUCH_PROTOCOL_NAME_SWIP)) {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unknown protocol mode");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_ensure_panel_info(FuLxsTouchDevice *self, GError **error)
{
	g_autoptr(FuStructLxsTouchPanel) st = NULL;
	guint8 buf[8] = {0};

	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_lxs_touch_device_read_data(self,
					   FU_LXS_TOUCH_REG_INFO_PANEL,
					   buf,
					   sizeof(buf),
					   error))
		return FALSE;
	st = fu_struct_lxs_touch_panel_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	self->x_size = fu_struct_lxs_touch_panel_get_x_node(st);
	self->y_size = fu_struct_lxs_touch_panel_get_y_node(st);

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_ensure_4k_mode(FuLxsTouchDevice *self, GError **error)
{
	guint8 buf[8] = {0};
	g_autoptr(FuStructLxsTouchFlashIapCmd) st_req = fu_struct_lxs_touch_flash_iap_cmd_new();
	g_autoptr(FuStructLxsTouchFlashIapCmd) st_res = NULL;

	fu_struct_lxs_touch_flash_iap_cmd_set_cmd(st_req, FU_LXS_TOUCH_CMD_FLASH_4KB_UPDATE_MODE);
	if (!fu_lxs_touch_device_write(self,
				       FU_LXS_TOUCH_REG_FLASH_IAP_CTRL_CMD,
				       st_req->buf->data,
				       st_req->buf->len,
				       error))
		return FALSE;
	if (!fu_lxs_touch_device_wait_ready(self, error))
		return FALSE;
	if (!fu_lxs_touch_device_read_data(self,
					   FU_LXS_TOUCH_REG_FLASH_IAP_CTRL_CMD,
					   buf,
					   sizeof(buf),
					   error))
		return FALSE;
	st_res = fu_struct_lxs_touch_flash_iap_cmd_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL)
		return FALSE;
	self->use_4k_mode = fu_struct_lxs_touch_flash_iap_cmd_get_status(st_res) != 0;

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_set_mode(FuLxsTouchDevice *self, FuLxsTouchMode mode, GError **error)
{
	g_autoptr(FuStructLxsTouchSetter) st = NULL;
	guint8 buf[FU_STRUCT_LXS_TOUCH_SETTER_SIZE] = {0};

	if (!fu_lxs_touch_device_read_data(self,
					   FU_LXS_TOUCH_REG_CTRL_SETTER,
					   buf,
					   sizeof(buf),
					   NULL)) {
		st = fu_struct_lxs_touch_setter_new();
		fu_struct_lxs_touch_setter_set_mode(st, mode);
	} else {
		st = fu_struct_lxs_touch_setter_parse(buf, sizeof(buf), 0x0, error);
		if (st == NULL)
			return FALSE;
		fu_struct_lxs_touch_setter_set_mode(st, mode);
	}

	if (!fu_lxs_touch_device_write(self,
				       FU_LXS_TOUCH_REG_CTRL_SETTER,
				       st->buf->data,
				       st->buf->len,
				       error))
		return FALSE;

	if (mode == FU_LXS_TOUCH_MODE_DFUP)
		fu_device_sleep(FU_DEVICE(self), 500);

	if (!fu_lxs_touch_device_wait_ready(self, error))
		return FALSE;

	if (mode == FU_LXS_TOUCH_MODE_DFUP) {
		if (!fu_lxs_touch_device_ensure_4k_mode(self, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_setup(FuDevice *device, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);

	if (!fu_lxs_touch_device_ensure_mode(self, error))
		return FALSE;
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_lxs_touch_device_ensure_version(self, error))
			return FALSE;
		if (!fu_lxs_touch_device_ensure_panel_info(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	if (!fu_lxs_touch_device_set_mode(self, FU_LXS_TOUCH_MODE_DFUP, error)) {
		g_prefix_error_literal(error, "failed to enter DFUP mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_normal_chunk(FuLxsTouchDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(FuStructLxsTouchFlashIapCmd) st_req = fu_struct_lxs_touch_flash_iap_cmd_new();
	g_autoptr(GPtrArray) blocks = NULL;

	blocks = fu_chunk_array_new(fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    FU_LXS_TOUCH_REG_PARAMETER_BUFFER,
				    FU_CHUNK_PAGESZ_NONE,
				    FU_LXS_TOUCH_TRANSMIT_UNIT_NORMAL,
				    error);
	if (blocks == NULL)
		return FALSE;
	for (guint i = 0; i < blocks->len; i++) {
		FuChunk *blk = g_ptr_array_index(blocks, i);
		if (!fu_lxs_touch_device_write(self,
					       fu_chunk_get_address(blk),
					       fu_chunk_get_data(blk),
					       fu_chunk_get_data_sz(blk),
					       error))
			return FALSE;
	}
	fu_struct_lxs_touch_flash_iap_cmd_set_addr(st_req, fu_chunk_get_address(chk));
	fu_struct_lxs_touch_flash_iap_cmd_set_size(st_req, fu_chunk_get_data_sz(chk));
	fu_struct_lxs_touch_flash_iap_cmd_set_status(st_req, 0);
	fu_struct_lxs_touch_flash_iap_cmd_set_cmd(st_req, FU_LXS_TOUCH_CMD_FLASH_WRITE);
	if (!fu_lxs_touch_device_write(self,
				       FU_LXS_TOUCH_REG_FLASH_IAP_CTRL_CMD,
				       st_req->buf->data,
				       st_req->buf->len,
				       error))
		return FALSE;
	if (!fu_lxs_touch_device_wait_ready(self, error)) {
		g_prefix_error_literal(error, "flash write failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_normal_chunks(FuLxsTouchDevice *self,
					FuChunkArray *chunks,
					FuProgress *progress,
					GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_lxs_touch_device_write_normal_chunk(self, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_4k_chunk(FuLxsTouchDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(FuStructLxsTouchFlashIapCmd) st_req = fu_struct_lxs_touch_flash_iap_cmd_new();
	g_autoptr(GPtrArray) blocks = NULL;

	blocks = fu_chunk_array_new(fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    FU_LXS_TOUCH_REG_PARAMETER_BUFFER,
				    FU_CHUNK_PAGESZ_NONE,
				    FU_LXS_TOUCH_TRANSMIT_UNIT_4K,
				    error);
	if (blocks == NULL)
		return FALSE;
	for (guint i = 0; i < blocks->len; i++) {
		FuChunk *blk = g_ptr_array_index(blocks, i);
		if (!fu_lxs_touch_device_write(self,
					       fu_chunk_get_address(blk),
					       fu_chunk_get_data(blk),
					       fu_chunk_get_data_sz(blk),
					       error))
			return FALSE;
	}
	fu_struct_lxs_touch_flash_iap_cmd_set_addr(st_req, fu_chunk_get_address(chk));
	fu_struct_lxs_touch_flash_iap_cmd_set_size(st_req, fu_chunk_get_data_sz(chk));
	fu_struct_lxs_touch_flash_iap_cmd_set_cmd(st_req, FU_LXS_TOUCH_CMD_FLASH_WRITE);
	if (!fu_lxs_touch_device_write(self,
				       FU_LXS_TOUCH_REG_FLASH_IAP_CTRL_CMD,
				       st_req->buf->data,
				       st_req->buf->len,
				       error))
		return FALSE;
	if (!fu_lxs_touch_device_wait_ready(self, error)) {
		g_prefix_error_literal(error, "flash write failed in 4K mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_4k_chunk_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	FuChunk *chk = (FuChunk *)user_data;
	return fu_lxs_touch_device_write_4k_chunk(self, chk, error);
}

static gboolean
fu_lxs_touch_device_write_4k_chunks(FuLxsTouchDevice *self,
				    FuChunkArray *chunks,
				    FuProgress *progress,
				    GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_lxs_touch_device_write_4k_chunk_cb,
				     5,
				     chk,
				     error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lxs_touch_device_reset_wdt(FuLxsTouchDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuStructLxsTouchSetter) st = fu_struct_lxs_touch_setter_new();

	fu_struct_lxs_touch_setter_set_mode(st, (FuLxsTouchMode)FU_LXS_TOUCH_CMD_WATCHDOG_RESET);
	if (!fu_lxs_touch_device_write(self,
				       FU_LXS_TOUCH_REG_CTRL_SETTER,
				       st->buf->data,
				       st->buf->len,
				       &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_BUSY)) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to reset WDT: ");
			return FALSE;
		}
		g_debug("ignoring watchdog reset failure: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_lxs_touch_device_get_protocol_name(FuLxsTouchDevice *self, GError **error)
{
	guint8 buf[8] = {0};
	g_autoptr(FuStructLxsTouchInterface) st = NULL;

	if (!fu_lxs_touch_device_read_data(self,
					   FU_LXS_TOUCH_REG_INFO_INTERFACE,
					   buf,
					   sizeof(buf),
					   error))
		return NULL;
	st = fu_struct_lxs_touch_interface_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return NULL;
	return fu_struct_lxs_touch_interface_get_protocol_name(st);
}

static gboolean
fu_lxs_touch_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	guint32 fw_offset = fu_lxs_touch_firmware_get_offset(FU_LXS_TOUCH_FIRMWARE(firmware));
	g_autoptr(GInputStream) stream = NULL;
	g_autofree gchar *protocol_name = NULL;

	/* verify in DFUP mode */
	protocol_name = fu_lxs_touch_device_get_protocol_name(self, error);
	if (protocol_name == NULL)
		return FALSE;
	if (!g_str_has_prefix(protocol_name, FU_LXS_TOUCH_PROTOCOL_NAME_DFUP)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device not in DFUP mode");
		return FALSE;
	}

	/* send chunks */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (self->use_4k_mode) {
		g_autoptr(FuChunkArray) chunks = NULL;
		chunks = fu_chunk_array_new_from_stream(stream,
							fw_offset,
							0x0,
							FU_LXS_TOUCH_DOWNLOAD_CHUNK_SIZE_4K,
							error);
		if (chunks == NULL)
			return FALSE;
		if (!fu_lxs_touch_device_write_4k_chunks(self, chunks, progress, error))
			return FALSE;
	} else {
		g_autoptr(FuChunkArray) chunks = NULL;
		chunks = fu_chunk_array_new_from_stream(stream,
							fw_offset,
							0x0,
							FU_LXS_TOUCH_DOWNLOAD_CHUNK_SIZE_NORMAL,
							error);
		if (chunks == NULL)
			return FALSE;
		if (!fu_lxs_touch_device_write_normal_chunks(self, chunks, progress, error))
			return FALSE;
	}
	if (!fu_lxs_touch_device_reset_wdt(self, error))
		return FALSE;

	/* I2C HID devices do not disconnect/reconnect after watchdog reset;
	 * the same /dev/hidrawN node remains. Wait for the device to settle. */
	fu_device_sleep(device, 3000);
	return TRUE;
}

static void
fu_lxs_touch_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-firmware");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_lxs_touch_device_init(FuLxsTouchDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lxsemicon.touch");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_install_duration(FU_DEVICE(self), 60);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LXS_TOUCH_FIRMWARE);
}

static void
fu_lxs_touch_device_class_init(FuLxsTouchDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_lxs_touch_device_setup;
	device_class->reload = fu_lxs_touch_device_setup;
	device_class->detach = fu_lxs_touch_device_detach;
	device_class->write_firmware = fu_lxs_touch_device_write_firmware;
	device_class->to_string = fu_lxs_touch_device_to_string;
	device_class->set_progress = fu_lxs_touch_device_set_progress;
}
