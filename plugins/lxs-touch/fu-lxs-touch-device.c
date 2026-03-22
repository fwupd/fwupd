/*
 * Copyright 2026 JS Park <mameforever2@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-lxs-touch-device.h"
#include "fu-lxs-touch-firmware.h"
#include "fu-lxs-touch-struct.h"

#define FU_LXSTOUCH_PROTOCOL_NAME_DFUP "DFUP"
#define FU_LXSTOUCH_PROTOCOL_NAME_SWIP "SWIP"

struct _FuLxsTouchDevice {
	FuHidrawDevice parent_instance;
	gboolean is_dfup_mode;
	gboolean use_4k_mode;
	guint8 x_size;
	guint8 y_size;
};

G_DEFINE_TYPE(FuLxsTouchDevice, fu_lxs_touch_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_lxs_touch_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	fwupd_codec_string_append_bool(str, idt, "IsDfupMode", self->is_dfup_mode);
	fwupd_codec_string_append_bool(str, idt, "Use4kMode", self->use_4k_mode);
	fwupd_codec_string_append_int(str, idt, "XSize", self->x_size);
	fwupd_codec_string_append_int(str, idt, "YSize", self->y_size);
}

static gboolean
fu_lxs_touch_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));

	if (subsystem == NULL || g_strcmp0(subsystem, "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device has incorrect subsystem=%s, expected hidraw",
			    subsystem != NULL ? subsystem : "(null)");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_command(FuLxsTouchDevice *self,
								 guint8 flag,
								 guint16 command,
								 guint16 length,
								 const guint8 *data,
								 GError **error)
{
	FuStructLxsTouchPacket *packet = fu_struct_lxs_touch_packet_new();
	guint8 buf[FU_LXSTOUCH_BUFFER_SIZE] = {0};
	guint16 adjusted_length = length;

	if (flag == FU_LXSTOUCH_FLAG_WRITE)
		adjusted_length += 2;

	fu_struct_lxs_touch_packet_set_report_id(packet, FU_LXSTOUCH_REPORT_ID);
	fu_struct_lxs_touch_packet_set_flag(packet, flag);
	fu_struct_lxs_touch_packet_set_length_lo(packet, (guint8)(adjusted_length & 0x00FF));
	fu_struct_lxs_touch_packet_set_length_hi(packet, (guint8)((adjusted_length & 0xFF00) >> 8));
	fu_struct_lxs_touch_packet_set_command_hi(packet, (guint8)((command & 0xFF00) >> 8));
	fu_struct_lxs_touch_packet_set_command_lo(packet, (guint8)(command & 0x00FF));

	memcpy(buf, packet, sizeof(FuStructLxsTouchPacket));

	if (flag == FU_LXSTOUCH_FLAG_WRITE && data != NULL && length > 0) {
		if (length > FU_LXSTOUCH_BUFFER_SIZE - sizeof(FuStructLxsTouchPacket)) {
			g_set_error(error,
						FWUPD_ERROR,
						FWUPD_ERROR_INTERNAL,
						"data length %u exceeds buffer limit",
						length);
			return FALSE;
		}
		memcpy(&buf[sizeof(FuStructLxsTouchPacket)], data, length);
	}

	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
									   buf,
									   FU_LXSTOUCH_BUFFER_SIZE,
									   FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
									   error);
}

static gboolean
fu_lxs_touch_device_read_data(FuLxsTouchDevice *self,
			     guint16 command,
			     guint16 length,
			     guint8 *data,
			     GError **error)
{
	guint8 buf[FU_LXSTOUCH_BUFFER_SIZE] = {0};

	/* 0x68 */
	if (!fu_lxs_touch_device_write_command(self,
					      FU_LXSTOUCH_FLAG_WRITE,
					      command,
					      0,
					      NULL,
					      error))
		return FALSE;

	/* 0x69 */
	if (!fu_lxs_touch_device_write_command(self,
					      FU_LXSTOUCH_FLAG_READ,
					      command,
					      length,
					      NULL,
					      error))
		return FALSE;

	if (!fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self),
					 buf,
					 FU_LXSTOUCH_BUFFER_SIZE,
					 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					 error))
		return FALSE;

	if (data != NULL && length > 0) {
		if (4 + length > FU_LXSTOUCH_BUFFER_SIZE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "read length %u exceeds buffer",
				    length);
			return FALSE;
		}
		memcpy(data, &buf[4], length);
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_wait_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	g_autoptr(FuStructLxsTouchGetter) st_getter = NULL;
	guint8 buf[8] = {0};

	if (!fu_lxs_touch_device_read_data(self,
					  FU_LXSTOUCH_REG_CTRL_GETTER,
					  sizeof(buf),
					  buf,
					  error))
		return FALSE;

	st_getter = fu_struct_lxs_touch_getter_parse(buf, sizeof(buf), 0x0, error);
	if (st_getter == NULL)
		return FALSE;

	if (fu_struct_lxs_touch_getter_get_ready_status(st_getter) ==
	    FU_LXSTOUCH_READY_STATUS_READY)
		return TRUE;

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "device not ready");
	return FALSE;
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

static gchar *
fu_lxs_touch_device_read_version(FuLxsTouchDevice *self, GError **error)
{
	g_autoptr(FuStructLxsTouchVersion) st_ver = NULL;
	guint8 buf[8] = {0};

	if (!fu_lxs_touch_device_read_data(self,
					  FU_LXSTOUCH_REG_INFO_VERSION,
					  sizeof(buf),
					  buf,
					  error))
		return NULL;

	st_ver = fu_struct_lxs_touch_version_parse(buf, sizeof(buf), 0x0, error);
	if (st_ver == NULL)
		return NULL;

	return g_strdup_printf(
	    "%u.%u.%u.%u",
	    fu_struct_lxs_touch_version_get_boot_ver(st_ver),
	    fu_struct_lxs_touch_version_get_core_ver(st_ver),
	    fu_struct_lxs_touch_version_get_app_ver(st_ver),
	    fu_struct_lxs_touch_version_get_param_ver(st_ver));
}

static gboolean
fu_lxs_touch_device_check_mode(FuLxsTouchDevice *self, GError **error)
{
	g_autoptr(FuStructLxsTouchInterface) st_iface = NULL;
	g_autofree gchar *protocol_name = NULL;
	guint8 buf[8] = {0};

	if (!fu_lxs_touch_device_read_data(self,
					  FU_LXSTOUCH_REG_INFO_INTERFACE,
					  sizeof(buf),
					  buf,
					  error))
		return FALSE;

	st_iface = fu_struct_lxs_touch_interface_parse(buf, sizeof(buf), 0x0, error);
	if (st_iface == NULL)
		return FALSE;

	protocol_name = fu_struct_lxs_touch_interface_get_protocol_name(st_iface);
	if (g_str_has_prefix(protocol_name, FU_LXSTOUCH_PROTOCOL_NAME_DFUP)) {
		self->is_dfup_mode = TRUE;
		g_debug("device is in DFUP mode");
	} else if (g_str_has_prefix(protocol_name, FU_LXSTOUCH_PROTOCOL_NAME_SWIP)) {
		self->is_dfup_mode = FALSE;
		g_debug("device is in SWIP mode");
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unknown protocol mode");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_read_panel_info(FuLxsTouchDevice *self, GError **error)
{
	g_autoptr(FuStructLxsTouchPanel) st_panel = NULL;
	guint8 buf[8] = {0};

	if (self->is_dfup_mode)
		return TRUE;

	if (!fu_lxs_touch_device_read_data(self,
					  FU_LXSTOUCH_REG_INFO_PANEL,
					  sizeof(buf),
					  buf,
					  error))
		return FALSE;

	st_panel = fu_struct_lxs_touch_panel_parse(buf, sizeof(buf), 0x0, error);
	if (st_panel == NULL)
		return FALSE;

	self->x_size = fu_struct_lxs_touch_panel_get_x_node(st_panel);
	self->y_size = fu_struct_lxs_touch_panel_get_y_node(st_panel);
	g_debug("panel size: X=%u, Y=%u", self->x_size, self->y_size);

	return TRUE;
}

static gboolean
fu_lxs_touch_device_check_4k_mode(FuLxsTouchDevice *self, GError **error)
{
	g_autoptr(FuStructLxsTouchFlashIapCmd) st_cmd = fu_struct_lxs_touch_flash_iap_cmd_new();
	guint8 buf[8] = {0};

	fu_struct_lxs_touch_flash_iap_cmd_set_cmd(st_cmd, FU_LXSTOUCH_CMD_FLASH_4KB_UPDATE_MODE);
	if (!fu_lxs_touch_device_write_command(self,
					      FU_LXSTOUCH_FLAG_WRITE,
					      FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD,
					      FU_STRUCT_LXS_TOUCH_FLASH_IAP_CMD_SIZE,
					      st_cmd->buf->data,
					      error))
		return FALSE;

	if (!fu_lxs_touch_device_wait_ready(self, error))
		return FALSE;

	if (!fu_lxs_touch_device_read_data(self,
					  FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD,
					  sizeof(buf),
					  buf,
					  error))
		return FALSE;

	st_cmd = fu_struct_lxs_touch_flash_iap_cmd_parse(buf, sizeof(buf), 0x0, error);
	if (st_cmd == NULL)
		return FALSE;

	self->use_4k_mode = fu_struct_lxs_touch_flash_iap_cmd_get_status(st_cmd) != 0;
	g_debug("4K mode: %s", self->use_4k_mode ? "enabled" : "disabled");

	return TRUE;
}

static gboolean
fu_lxs_touch_device_set_mode(FuLxsTouchDevice *self, guint8 mode, GError **error)
{
	g_autoptr(FuStructLxsTouchSetter) st_setter = NULL;
	guint8 buf[FU_STRUCT_LXS_TOUCH_SETTER_SIZE] = {0};

	if (!fu_lxs_touch_device_read_data(self,
					  FU_LXSTOUCH_REG_CTRL_SETTER,
					  sizeof(buf),
					  buf,
					  NULL)) {
		st_setter = fu_struct_lxs_touch_setter_new();
		fu_struct_lxs_touch_setter_set_mode(st_setter, mode);
		fu_struct_lxs_touch_setter_set_event_trigger_type(st_setter, 0);
	} else {
		st_setter = fu_struct_lxs_touch_setter_parse(buf, sizeof(buf), 0x0, error);
		if (st_setter == NULL)
			return FALSE;
		fu_struct_lxs_touch_setter_set_mode(st_setter, mode);
	}

	if (!fu_lxs_touch_device_write_command(self,
					      FU_LXSTOUCH_FLAG_WRITE,
					      FU_LXSTOUCH_REG_CTRL_SETTER,
					      FU_STRUCT_LXS_TOUCH_SETTER_SIZE,
					      st_setter->buf->data,
					      error))
		return FALSE;

	if (!fu_lxs_touch_device_wait_ready(self, error))
		return FALSE;

	if (mode == FU_LXSTOUCH_MODE_DFUP) {
		if (!fu_lxs_touch_device_check_4k_mode(self, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_setup(FuDevice *device, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	g_autofree gchar *version = NULL;

	if (!fu_lxs_touch_device_check_mode(self, error))
		return FALSE;

	if (self->is_dfup_mode) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	} else {
		version = fu_lxs_touch_device_read_version(self, error);
		if (version == NULL)
			return FALSE;
		fu_device_set_version(device, version);

		if (!fu_lxs_touch_device_read_panel_info(self, error))
			return FALSE;

		if (!fu_lxs_touch_device_set_mode(self, FU_LXSTOUCH_MODE_NORMAL, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);

	if (self->is_dfup_mode)
		return TRUE;

	if (!fu_lxs_touch_device_set_mode(self, FU_LXSTOUCH_MODE_DFUP, error)) {
		g_prefix_error_literal(error, "failed to enter DFUP mode: ");
		return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_normal_mode(FuLxsTouchDevice *self,
				     FuChunkArray *chunks,
				     guint32 fw_offset,
				     FuProgress *progress,
				     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuStructLxsTouchFlashIapCmd) st_cmd =
		    fu_struct_lxs_touch_flash_iap_cmd_new();
		guint32 flash_addr;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		flash_addr = fw_offset + fu_chunk_get_address(chk);

		for (guint j = 0; j < fu_chunk_get_data_sz(chk);
		     j += FU_LXSTOUCH_TRANSMIT_UNIT_NORMAL) {
			guint write_size = MIN(FU_LXSTOUCH_TRANSMIT_UNIT_NORMAL,
					       fu_chunk_get_data_sz(chk) - j);
			if (!fu_lxs_touch_device_write_command(
				self,
				FU_LXSTOUCH_FLAG_WRITE,
				FU_LXSTOUCH_REG_PARAMETER_BUFFER + j,
				write_size,
				fu_chunk_get_data(chk) + j,
				error))
				return FALSE;
		}

		fu_struct_lxs_touch_flash_iap_cmd_set_addr(st_cmd, flash_addr);
		fu_struct_lxs_touch_flash_iap_cmd_set_size(st_cmd, fu_chunk_get_data_sz(chk));
		fu_struct_lxs_touch_flash_iap_cmd_set_status(st_cmd, 0);
		fu_struct_lxs_touch_flash_iap_cmd_set_cmd(st_cmd, FU_LXSTOUCH_CMD_FLASH_WRITE);

		if (!fu_lxs_touch_device_write_command(self,
						      FU_LXSTOUCH_FLAG_WRITE,
						      FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD,
						      FU_STRUCT_LXS_TOUCH_FLASH_IAP_CMD_SIZE,
						      st_cmd->buf->data,
						      error))
			return FALSE;

		if (!fu_lxs_touch_device_wait_ready(self, error)) {
			g_prefix_error_literal(error, "flash write failed: ");
			return FALSE;
		}

		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_4k_mode(FuLxsTouchDevice *self,
				 FuChunkArray *chunks,
				 guint32 fw_offset,
				 FuProgress *progress,
				 GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuStructLxsTouchFlashIapCmd) st_cmd =
		    fu_struct_lxs_touch_flash_iap_cmd_new();
		guint32 flash_addr;
		guint32 normal_size;
		guint32 last_unit_size;
		guint32 crc;
		guint8 verify_buf[8] = {0};
		guint retry_count = 0;

	retry_write:
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		flash_addr = fw_offset + fu_chunk_get_address(chk);
		last_unit_size = fu_chunk_get_data_sz(chk) % FU_LXSTOUCH_TRANSMIT_UNIT_4K;
		normal_size = fu_chunk_get_data_sz(chk) - last_unit_size;

		for (guint j = 0; j < normal_size; j += FU_LXSTOUCH_TRANSMIT_UNIT_4K) {
			if (!fu_lxs_touch_device_write_command(
				self,
				FU_LXSTOUCH_FLAG_WRITE,
				FU_LXSTOUCH_REG_PARAMETER_BUFFER + j,
				FU_LXSTOUCH_TRANSMIT_UNIT_4K,
				fu_chunk_get_data(chk) + j,
				error))
				return FALSE;
		}

		if (last_unit_size > 0) {
			if (!fu_lxs_touch_device_write_command(
				self,
				FU_LXSTOUCH_FLAG_WRITE,
				FU_LXSTOUCH_REG_PARAMETER_BUFFER + normal_size,
				last_unit_size,
				fu_chunk_get_data(chk) + normal_size,
				error))
				return FALSE;
		}

		fu_struct_lxs_touch_flash_iap_cmd_set_addr(st_cmd, flash_addr);
		fu_struct_lxs_touch_flash_iap_cmd_set_size(st_cmd, fu_chunk_get_data_sz(chk));
		fu_struct_lxs_touch_flash_iap_cmd_set_status(st_cmd, 0);
		fu_struct_lxs_touch_flash_iap_cmd_set_cmd(st_cmd, FU_LXSTOUCH_CMD_FLASH_WRITE);

		if (!fu_lxs_touch_device_write_command(self,
						      FU_LXSTOUCH_FLAG_WRITE,
						      FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD,
						      FU_STRUCT_LXS_TOUCH_FLASH_IAP_CMD_SIZE,
						      st_cmd->buf->data,
						      error))
			return FALSE;

		if (!fu_lxs_touch_device_wait_ready(self, error)) {
			g_prefix_error_literal(error, "flash write failed in 4K mode: ");
			return FALSE;
		}

		/* CRC verification for 4K mode writes */
		crc = 0xFFFFFFFF;
		for (guint j = 0; j < fu_chunk_get_data_sz(chk); j++) {
			crc += fu_chunk_get_data(chk)[j];
		}
		crc ^= 0xFFFFFFFF;

		if (!fu_lxs_touch_device_write_command(self,
						      FU_LXSTOUCH_FLAG_WRITE,
						      FU_LXSTOUCH_REG_PARAMETER_BUFFER,
						      4,
						      (guint8 *)&crc,
						      error))
			return FALSE;

		fu_struct_lxs_touch_flash_iap_cmd_set_cmd(st_cmd,
							 FU_LXSTOUCH_CMD_FLASH_GET_VERIFY);
		if (!fu_lxs_touch_device_write_command(self,
						      FU_LXSTOUCH_FLAG_WRITE,
						      FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD,
						      FU_STRUCT_LXS_TOUCH_FLASH_IAP_CMD_SIZE,
						      st_cmd->buf->data,
						      error))
			return FALSE;

		if (!fu_lxs_touch_device_wait_ready(self, error))
			return FALSE;

		memset(verify_buf, 0, sizeof(verify_buf));
		if (!fu_lxs_touch_device_read_data(self,
						  FU_LXSTOUCH_REG_FLASH_IAP_CTRL_CMD,
						  sizeof(verify_buf),
						  verify_buf,
						  error))
			return FALSE;

		st_cmd = fu_struct_lxs_touch_flash_iap_cmd_parse(verify_buf,
								 sizeof(verify_buf),
								 0x0,
								 error);
		if (st_cmd == NULL)
			return FALSE;

		if (fu_struct_lxs_touch_flash_iap_cmd_get_status(st_cmd) == 0) {
			retry_count++;
			if (retry_count >= 5) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_WRITE,
						    "flash verification failed after 5 retries");
				return FALSE;
			}
			g_debug("verification failed, retrying (%u/5)", retry_count);
			goto retry_write;
		}

		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	FuLxsTouchFirmware *fw = FU_LXS_TOUCH_FIRMWARE(firmware);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuStructLxsTouchInterface) st_iface = NULL;
	g_autoptr(FuStructLxsTouchSetter) st_setter = NULL;
	g_autofree gchar *protocol_name = NULL;
	guint32 fw_offset;
	guint32 chunk_size;
	guint8 buf[8] = {0};

	if (!fu_lxs_touch_device_read_data(self,
					  FU_LXSTOUCH_REG_INFO_INTERFACE,
					  sizeof(buf),
					  buf,
					  error))
		return FALSE;

	st_iface = fu_struct_lxs_touch_interface_parse(buf, sizeof(buf), 0x0, error);
	if (st_iface == NULL)
		return FALSE;

	protocol_name = fu_struct_lxs_touch_interface_get_protocol_name(st_iface);
	if (!g_str_has_prefix(protocol_name, FU_LXSTOUCH_PROTOCOL_NAME_DFUP)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device not in DFUP mode");
		return FALSE;
	}

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	fw_offset = fu_lxs_touch_firmware_get_offset(fw);

	chunk_size = self->use_4k_mode ? FU_LXSTOUCH_DOWNLOAD_CHUNK_SIZE_4K
				       : FU_LXSTOUCH_DOWNLOAD_CHUNK_SIZE_NORMAL;

	chunks = fu_chunk_array_new_from_stream(stream, 0x0, 0x0, chunk_size, error);
	if (chunks == NULL)
		return FALSE;

	g_debug("writing firmware: offset=0x%x, chunks=%u, chunk_size=%u, mode=%s",
		fw_offset,
		fu_chunk_array_length(chunks),
		chunk_size,
		self->use_4k_mode ? "4K" : "normal");

	if (self->use_4k_mode) {
		if (!fu_lxs_touch_device_write_4k_mode(self, chunks, fw_offset, progress, error))
			return FALSE;
	} else {
		if (!fu_lxs_touch_device_write_normal_mode(self,
							  chunks,
							  fw_offset,
							  progress,
							  error))
			return FALSE;
	}

	st_setter = fu_struct_lxs_touch_setter_new();
	fu_struct_lxs_touch_setter_set_mode(st_setter, FU_LXSTOUCH_CMD_WATCHDOG_RESET);
	fu_struct_lxs_touch_setter_set_event_trigger_type(st_setter, 0);
	if (!fu_lxs_touch_device_write_command(self,
					      FU_LXSTOUCH_FLAG_WRITE,
					      FU_LXSTOUCH_REG_CTRL_SETTER,
					      FU_STRUCT_LXS_TOUCH_SETTER_SIZE,
					      st_setter->buf->data,
					      NULL)) {
		g_debug("watchdog reset command failed (expected)");
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_lxs_touch_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_lxs_touch_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_lxs_touch_device_init(FuLxsTouchDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.lginnotek.lxstouch");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_install_duration(FU_DEVICE(self), 60);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LXS_TOUCH_FIRMWARE);
	self->is_dfup_mode = FALSE;
	self->use_4k_mode = FALSE;
}

static void
fu_lxs_touch_device_class_init(FuLxsTouchDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_lxs_touch_device_probe;
	device_class->setup = fu_lxs_touch_device_setup;
	device_class->detach = fu_lxs_touch_device_detach;
	device_class->attach = fu_lxs_touch_device_attach;
	device_class->write_firmware = fu_lxs_touch_device_write_firmware;
	device_class->to_string = fu_lxs_touch_device_to_string;
	device_class->set_progress = fu_lxs_touch_device_set_progress;
}
