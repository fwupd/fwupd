/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-wacom-common.h"
#include "fu-wacom-device.h"

typedef struct {
	guint flash_block_size;
	guint32 flash_base_addr;
	guint8 echo_next;
} FuWacomDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuWacomDevice, fu_wacom_device, FU_TYPE_HIDRAW_DEVICE)

#define GET_PRIVATE(o) (fu_wacom_device_get_instance_private(o))

static void
fu_wacom_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	FuWacomDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "FlashBlockSize", priv->flash_block_size);
	fwupd_codec_string_append_hex(str, idt, "FlashBaseAddr", priv->flash_base_addr);
	fwupd_codec_string_append_hex(str, idt, "EchoNext", priv->echo_next);
}

#define FU_WACOM_RAW_ECHO_MIN 0xA0
#define FU_WACOM_RAW_ECHO_MAX 0xFE

guint8
fu_wacom_device_get_echo_next(FuWacomDevice *self)
{
	FuWacomDevicePrivate *priv = GET_PRIVATE(self);
	priv->echo_next++;
	if (priv->echo_next > FU_WACOM_RAW_ECHO_MAX)
		priv->echo_next = FU_WACOM_RAW_ECHO_MIN;
	return priv->echo_next;
}

gsize
fu_wacom_device_get_block_sz(FuWacomDevice *self)
{
	FuWacomDevicePrivate *priv = GET_PRIVATE(self);
	return priv->flash_block_size;
}

gboolean
fu_wacom_device_check_mpu(FuWacomDevice *self, GError **error)
{
	guint8 rsp_value = 0;
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_GET_MPUTYPE);
	fu_struct_wacom_raw_request_set_echo(st_req, fu_wacom_device_get_echo_next(self));
	if (!fu_wacom_device_cmd(self,
				 st_req,
				 &rsp_value,
				 0,
				 FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK,
				 error)) {
		g_prefix_error(error, "failed to get MPU type: ");
		return FALSE;
	}

	/* W9013 */
	if (rsp_value == 0x2e) {
		fu_device_add_instance_id_full(FU_DEVICE(self),
					       "WacomEMR_W9013",
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		return TRUE;
	}

	/* W9021 */
	if (rsp_value == 0x45) {
		fu_device_add_instance_id_full(FU_DEVICE(self),
					       "WacomEMR_W9021",
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		return TRUE;
	}

	/* unsupported */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "MPU is not W9013 or W9021: 0x%x",
		    rsp_value);
	return FALSE;
}

static gboolean
fu_wacom_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	g_autoptr(FuStructWacomRawFwDetachRequest) st = fu_struct_wacom_raw_fw_detach_request_new();
	g_autoptr(GError) error_local = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}
	if (!fu_wacom_device_set_feature(self, st->data, st->len, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
			g_debug("ignoring: %s", error_local->message);
		} else {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to switch to bootloader mode: ");
			return FALSE;
		}
	}

	/* does the device have to replug to bootloader mode */
	if (fu_device_has_private_flag(device, FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG)) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	} else {
		fu_device_sleep(device, 300); /* ms */
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
	return TRUE;
}

static gboolean
fu_wacom_device_check_mode(FuWacomDevice *self, GError **error)
{
	guint8 rsp_value = 0;
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_CHECK_MODE);
	fu_struct_wacom_raw_request_set_echo(st_req, fu_wacom_device_get_echo_next(self));
	if (!fu_wacom_device_cmd(self,
				 st_req,
				 &rsp_value,
				 0,
				 FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK,
				 error)) {
		g_prefix_error(error, "failed to check mode: ");
		return FALSE;
	}
	if (rsp_value != 0x06) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "check mode failed, mode=0x%02x",
			    rsp_value);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_wacom_device_set_version_bootloader(FuWacomDevice *self, GError **error)
{
	guint8 rsp_value = 0;
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructWacomRawRequest) st_req = fu_struct_wacom_raw_request_new();

	fu_struct_wacom_raw_request_set_report_id(st_req, FU_WACOM_RAW_BL_REPORT_ID_SET);
	fu_struct_wacom_raw_request_set_cmd(st_req, FU_WACOM_RAW_BL_CMD_GET_BLVER);
	fu_struct_wacom_raw_request_set_echo(st_req, fu_wacom_device_get_echo_next(self));
	if (!fu_wacom_device_cmd(self,
				 st_req,
				 &rsp_value,
				 0,
				 FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK,
				 error)) {
		g_prefix_error(error, "failed to get bootloader version: ");
		return FALSE;
	}
	version = g_strdup_printf("%u", rsp_value);
	fu_device_set_version_bootloader(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_wacom_device_write_firmware(FuDevice *device,
			       FuFirmware *firmware,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	FuWacomDevicePrivate *priv = GET_PRIVATE(self);
	FuWacomDeviceClass *klass = FU_WACOM_DEVICE_GET_CLASS(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* use the correct image from the firmware */
	g_debug("using element at addr 0x%0x", (guint)fu_firmware_get_addr(firmware));

	/* check start address and size */
	if (fu_firmware_get_addr(firmware) != priv->flash_base_addr) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "base addr invalid: 0x%05x",
			    (guint)fu_firmware_get_addr(firmware));
		return FALSE;
	}
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* we're in bootloader mode now */
	if (!fu_wacom_device_check_mode(self, error))
		return FALSE;
	if (!fu_wacom_device_set_version_bootloader(self, error))
		return FALSE;

	/* flash chunks */
	chunks = fu_chunk_array_new_from_bytes(fw,
					       priv->flash_base_addr,
					       FU_CHUNK_PAGESZ_NONE,
					       priv->flash_block_size);
	return klass->write_firmware(device, chunks, progress, error);
}

gboolean
fu_wacom_device_get_feature(FuWacomDevice *self, guint8 *data, guint datasz, GError **error)
{
	return fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					    data,
					    datasz,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

gboolean
fu_wacom_device_set_feature(FuWacomDevice *self, const guint8 *data, guint datasz, GError **error)
{
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    data,
					    datasz,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_wacom_device_cmd_response(FuWacomDevice *self,
			     const FuStructWacomRawRequest *st_req,
			     guint8 *rsp_value,
			     FuWacomDeviceCmdFlags flags,
			     GError **error)
{
	guint8 buf[FU_STRUCT_WACOM_RAW_RESPONSE_SIZE] = {FU_WACOM_RAW_BL_REPORT_ID_GET, 0x0};
	g_autoptr(FuStructWacomRawRequest) st_rsp = NULL;

	if (!fu_wacom_device_get_feature(self, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to receive: ");
		return FALSE;
	}
	st_rsp = fu_struct_wacom_raw_response_parse(buf, sizeof(buf), 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (!fu_wacom_common_check_reply(st_req, st_rsp, error))
		return FALSE;
	if ((flags & FU_WACOM_DEVICE_CMD_FLAG_NO_ERROR_CHECK) == 0) {
		if (!fu_wacom_common_rc_set_error(st_rsp, error))
			return FALSE;
	}

	/* optional */
	if (rsp_value != NULL)
		*rsp_value = fu_struct_wacom_raw_response_get_resp(st_rsp);

	/* success */
	return TRUE;
}

typedef struct {
	const FuStructWacomRawRequest *st_req;
	guint8 *rsp_value;
	FuWacomDeviceCmdFlags flags;
} FuWacomRawResponseHelper;

static gboolean
fu_wacom_device_cmd_response_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	FuWacomRawResponseHelper *helper = (FuWacomRawResponseHelper *)user_data;
	return fu_wacom_device_cmd_response(self,
					    helper->st_req,
					    helper->rsp_value,
					    helper->flags,
					    error);
}

gboolean
fu_wacom_device_cmd(FuWacomDevice *self,
		    const FuStructWacomRawRequest *st_req,
		    guint8 *rsp_value,
		    guint delay_ms,
		    FuWacomDeviceCmdFlags flags,
		    GError **error)
{
	if (!fu_wacom_device_set_feature(self, st_req->data, st_req->len, error)) {
		g_prefix_error(error, "failed to send: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), delay_ms);
	if (flags & FU_WACOM_DEVICE_CMD_FLAG_POLL_ON_WAITING) {
		FuWacomRawResponseHelper helper = {.st_req = st_req,
						   .rsp_value = rsp_value,
						   .flags = flags};
		return fu_device_retry_full(FU_DEVICE(self),
					    fu_wacom_device_cmd_response_cb,
					    FU_WACOM_RAW_CMD_RETRIES,
					    delay_ms,
					    &helper,
					    error);
	}
	return fu_wacom_device_cmd_response(self, st_req, rsp_value, flags, error);
}

static gboolean
fu_wacom_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuWacomDevice *self = FU_WACOM_DEVICE(device);
	FuWacomDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp = 0;

	if (g_strcmp0(key, "WacomI2cFlashBlockSize") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXSIZE, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->flash_block_size = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "WacomI2cFlashBaseAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->flash_base_addr = tmp;
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_wacom_device_replace(FuDevice *device, FuDevice *donor)
{
	g_return_if_fail(FU_IS_WACOM_DEVICE(device));
	g_return_if_fail(FU_IS_WACOM_DEVICE(donor));

	/* copy private instance data */
	if (fu_device_has_private_flag(donor, FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG)) {
		fu_device_add_private_flag(device,
					   FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG);
	}
}

static void
fu_wacom_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_wacom_device_init(FuWacomDevice *self)
{
	FuWacomDevicePrivate *priv = GET_PRIVATE(self);
	priv->echo_next = FU_WACOM_RAW_ECHO_MIN;
	fu_device_add_protocol(FU_DEVICE(self), "com.wacom.raw");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_IHEX_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_WACOM_RAW_DEVICE_FLAG_REQUIRES_WAIT_FOR_REPLUG);
}

static void
fu_wacom_device_class_init(FuWacomDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_wacom_device_to_string;
	device_class->write_firmware = fu_wacom_device_write_firmware;
	device_class->detach = fu_wacom_device_detach;
	device_class->set_quirk_kv = fu_wacom_device_set_quirk_kv;
	device_class->set_progress = fu_wacom_device_set_progress;
	device_class->replace = fu_wacom_device_replace;
}
