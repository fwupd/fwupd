/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-common.h"
#include "fu-dell-kestrel-rtshub-firmware.h"
#include "fu-dell-kestrel-rtshub-struct.h"
#include "fu-dell-kestrel-rtshub.h"

struct _FuDellKestrelRtsHub {
	FuHidDevice parent_instance;
	FuDellDockBaseType dock_type;
	gboolean fw_auth;
	gboolean dual_bank;
};

G_DEFINE_TYPE(FuDellKestrelRtsHub, fu_dell_kestrel_rtshub, FU_TYPE_HID_DEVICE)

static void
fu_dell_kestrel_rtshub_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDellKestrelRtsHub *self = FU_DELL_KESTREL_RTSHUB(device);
	fwupd_codec_string_append_bool(str, idt, "FwAuth", self->fw_auth);
	fwupd_codec_string_append_bool(str, idt, "DualBank", self->dual_bank);
	fwupd_codec_string_append_hex(str, idt, "DockType", self->dock_type);
}

static gboolean
fu_dell_kestrel_rtshub_set_clock_mode(FuDellKestrelRtsHub *self, gboolean enable, GError **error)
{
	g_autoptr(GByteArray) cmd_buf = fu_struct_rtshub_hid_cmd_buf_new();

	fu_struct_rtshub_hid_cmd_buf_set_cmd(cmd_buf, RTSHUB_CMD_WRITE_DATA);
	fu_struct_rtshub_hid_cmd_buf_set_ext(cmd_buf, RTSHUB_EXT_MCUMODIFYCLOCK);
	fu_struct_rtshub_hid_cmd_buf_set_regaddr(cmd_buf, (guint8)enable);
	fu_struct_rtshub_hid_cmd_buf_set_bufferlen(cmd_buf, 0);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      cmd_buf->data,
				      cmd_buf->len,
				      DELL_KESTREL_RTSHUB_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to set clock-mode=%i: ", enable);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_erase_spare_bank(FuDellKestrelRtsHub *self, GError **error)
{
	g_autoptr(GByteArray) cmd_buf = fu_struct_rtshub_hid_cmd_buf_new();

	fu_struct_rtshub_hid_cmd_buf_set_cmd(cmd_buf, RTSHUB_CMD_WRITE_DATA);
	fu_struct_rtshub_hid_cmd_buf_set_ext(cmd_buf, RTSHUB_EXT_ERASEBANK);
	fu_struct_rtshub_hid_cmd_buf_set_regaddr(cmd_buf, 0x0100);
	fu_struct_rtshub_hid_cmd_buf_set_bufferlen(cmd_buf, 0);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      cmd_buf->data,
				      cmd_buf->len,
				      DELL_KESTREL_RTSHUB_TIMEOUT * 3,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to erase spare bank: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_verify_update_fw(FuDellKestrelRtsHub *self,
					FuProgress *progress,
					GError **error)
{
	g_autoptr(GByteArray) cmd_buf = fu_struct_rtshub_hid_cmd_buf_new();

	fu_struct_rtshub_hid_cmd_buf_set_cmd(cmd_buf, RTSHUB_CMD_WRITE_DATA);
	fu_struct_rtshub_hid_cmd_buf_set_ext(cmd_buf, RTSHUB_EXT_VERIFYUPDATE);
	fu_struct_rtshub_hid_cmd_buf_set_regaddr(cmd_buf, 0x01);
	fu_struct_rtshub_hid_cmd_buf_set_bufferlen(cmd_buf, 0);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      cmd_buf->data,
				      cmd_buf->len,
				      DELL_KESTREL_RTSHUB_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;
	fu_device_sleep_full(FU_DEVICE(self), 4000, progress); /* ms */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x0,
				      cmd_buf->data,
				      cmd_buf->len,
				      DELL_KESTREL_RTSHUB_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;

	/* check device status, 1 for success otherwise fail */
	if (cmd_buf->data[0] != 0x01) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "firmware flash failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_write_flash(FuDellKestrelRtsHub *self,
				   guint32 addr,
				   const guint8 *data,
				   guint16 data_sz,
				   GError **error)
{
	g_autoptr(GByteArray) cmd_buf = fu_struct_rtshub_hid_cmd_buf_new();

	g_return_val_if_fail(data_sz <= 128, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(data_sz != 0, FALSE);

	fu_struct_rtshub_hid_cmd_buf_set_cmd(cmd_buf, RTSHUB_CMD_WRITE_DATA);
	fu_struct_rtshub_hid_cmd_buf_set_ext(cmd_buf, RTSHUB_EXT_WRITEFLASH);
	fu_struct_rtshub_hid_cmd_buf_set_regaddr(cmd_buf, addr);
	fu_struct_rtshub_hid_cmd_buf_set_bufferlen(cmd_buf, data_sz);
	if (!fu_struct_rtshub_hid_cmd_buf_set_data(cmd_buf, data, data_sz, error))
		return FALSE;

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      cmd_buf->data,
				      cmd_buf->len,
				      DELL_KESTREL_RTSHUB_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to write flash @%08x: ", (guint)addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuDellKestrelRtsHub *self = FU_DELL_KESTREL_RTSHUB(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 28, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 70, NULL);

	/* set MCU to high clock rate for better ISP performance */
	if (!fu_dell_kestrel_rtshub_set_clock_mode(self, TRUE, error))
		return FALSE;

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	g_debug("writing firmware: %s, %s -> %s",
		fu_device_get_name(device),
		fu_device_get_version(device),
		fu_firmware_get_version(firmware));

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						DELL_KESTREL_RTSHUB_TRANSFER_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	/* erase spare flash bank only if it is not empty */
	if (!fu_dell_kestrel_rtshub_erase_spare_bank(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* write chunk */
		if (!fu_dell_kestrel_rtshub_write_flash(self,
							fu_chunk_get_address(chk),
							fu_chunk_get_data(chk),
							fu_chunk_get_data_sz(chk),
							error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* get device to authenticate the firmware */
	if (!fu_dell_kestrel_rtshub_verify_update_fw(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_get_status(FuDevice *device, GError **error)
{
	FuDellKestrelRtsHub *self = FU_DELL_KESTREL_RTSHUB(device);
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) cmd_buf = fu_struct_rtshub_hid_cmd_buf_new();

	fu_struct_rtshub_hid_cmd_buf_set_cmd(cmd_buf, RTSHUB_CMD_READ_DATA);
	fu_struct_rtshub_hid_cmd_buf_set_ext(cmd_buf, RTSHUB_EXT_READ_STATUS);
	fu_struct_rtshub_hid_cmd_buf_set_regaddr(cmd_buf, 0x00);
	fu_struct_rtshub_hid_cmd_buf_set_bufferlen(cmd_buf, 12);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      cmd_buf->data,
				      cmd_buf->len,
				      DELL_KESTREL_RTSHUB_TIMEOUT,
				      FU_HID_DEVICE_FLAG_RETRY_FAILURE,
				      error))
		return FALSE;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x0,
				      cmd_buf->data,
				      cmd_buf->len,
				      DELL_KESTREL_RTSHUB_TIMEOUT,
				      FU_HID_DEVICE_FLAG_RETRY_FAILURE,
				      error))
		return FALSE;

	/* version: index 10, subversion: index 11 */
	version = g_strdup_printf("%x.%x", cmd_buf->data[10], cmd_buf->data[11]);
	fu_device_set_version(device, version);

	/* dual bank capability */
	self->dual_bank = (cmd_buf->data[13] & 0xf0) == 0x80;

	/* authentication capability */
	self->fw_auth = (cmd_buf->data[13] & 0x02) > 0;

	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_setup(FuDevice *device, GError **error)
{
	FuDellKestrelRtsHub *self = FU_DELL_KESTREL_RTSHUB(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dell_kestrel_rtshub_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_dell_kestrel_rtshub_get_status(device, error))
		return FALSE;

	if (self->dual_bank)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);

	if (!self->fw_auth)
		fu_device_set_update_error(device, "device does not support authentication");

	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_probe(FuDevice *device, GError **error)
{
	g_autofree const gchar *logical_id = NULL;
	FuDellKestrelRtsHub *self = FU_DELL_KESTREL_RTSHUB(device);

	/* not interesting */
	if (fu_device_get_vid(device) != DELL_VID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device vid not dell, expected: 0x%04x, got: 0x%04x",
			    (guint)DELL_VID,
			    fu_device_get_vid(device));
		return FALSE;
	}

	/* caring for my family back home after fw reset */
	switch (fu_device_get_pid(device)) {
	case DELL_KESTREL_USB_RTS0_G1_PID:
		fu_device_set_name(device, "RTS0 Gen 1 USB Hub");
		break;
	case DELL_KESTREL_USB_RTS0_G2_PID:
		fu_device_set_name(device, "RTS0 Gen 2 USB Hub");
		break;
	case DELL_KESTREL_USB_RTS5_G2_PID:
		fu_device_set_name(device, "RTS5 Gen 2 USB Hub");
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device pid '%04x' is not supported",
			    fu_device_get_pid(device));
		return FALSE;
	}

	/* build logical id */
	logical_id = g_strdup_printf("RTSHUB_%04X", fu_device_get_pid(device));
	fu_device_set_logical_id(device, logical_id);

	/* build instance id */
	fu_device_add_instance_u8(device, "DOCKTYPE", self->dock_type);
	fu_device_build_instance_id(device, error, "USB", "VID", "PID", "DOCKTYPE", NULL);
	return TRUE;
}

static gboolean
fu_dell_kestrel_rtshub_open(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);

	if (!FU_DEVICE_CLASS(fu_dell_kestrel_rtshub_parent_class)->open(device, error))
		return FALSE;

	if (parent != NULL)
		return fu_device_open(parent, error);
	return TRUE;
}

static void
fu_dell_kestrel_rtshub_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_kestrel_rtshub_init(FuDellKestrelRtsHub *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.kestrel");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_DELL_KESTREL_RTSHUB_FIRMWARE);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
}

static void
fu_dell_kestrel_rtshub_class_init(FuDellKestrelRtsHubClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_dell_kestrel_rtshub_to_string;
	device_class->setup = fu_dell_kestrel_rtshub_setup;
	device_class->probe = fu_dell_kestrel_rtshub_probe;
	device_class->write_firmware = fu_dell_kestrel_rtshub_write_firmware;
	device_class->set_progress = fu_dell_kestrel_rtshub_set_progress;
	device_class->open = fu_dell_kestrel_rtshub_open;
}

FuDellKestrelRtsHub *
fu_dell_kestrel_rtshub_new(FuUsbDevice *device, FuDellDockBaseType dock_type)
{
	FuDellKestrelRtsHub *self = g_object_new(FU_TYPE_DELL_KESTREL_RTSHUB, NULL);

	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device), FU_DEVICE_INCORPORATE_FLAG_ALL);
	self->dock_type = dock_type;
	return self;
}
