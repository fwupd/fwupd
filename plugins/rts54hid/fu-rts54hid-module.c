/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rts54hid-common.h"
#include "fu-rts54hid-device.h"
#include "fu-rts54hid-module.h"

struct _FuRts54HidModule {
	FuDevice parent_instance;
	guint8 target_addr;
	guint8 i2c_speed;
	guint8 register_addr_len;
};

G_DEFINE_TYPE(FuRts54HidModule, fu_rts54hid_module, FU_TYPE_DEVICE)

static void
fu_rts54hid_module_to_string(FuDevice *module, guint idt, GString *str)
{
	FuRts54HidModule *self = FU_RTS54HID_MODULE(module);
	fwupd_codec_string_append_hex(str, idt, "TargetAddr", self->target_addr);
	fwupd_codec_string_append_hex(str, idt, "I2cSpeed", self->i2c_speed);
	fwupd_codec_string_append_hex(str, idt, "RegisterAddrLen", self->register_addr_len);
}

static FuRts54HidDevice *
fu_rts54hid_module_get_parent(FuRts54HidModule *self, GError **error)
{
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no parent set");
		return NULL;
	}
	return FU_RTS54HID_DEVICE(parent);
}

static gboolean
fu_rts54hid_module_i2c_write(FuRts54HidModule *self,
			     const guint8 *data,
			     guint8 data_sz,
			     GError **error)
{
	FuRts54HidDevice *parent;
	const FuRts54HidCmdBuffer cmd_buffer = {
	    .cmd = FU_RTS54HID_CMD_WRITE_DATA,
	    .ext = FU_RTS54HID_EXT_I2C_WRITE,
	    .dwregaddr = 0,
	    .bufferlen = GUINT16_TO_LE(data_sz),
	    .parameters_i2c = {.target_addr = self->target_addr,
			       .data_sz = self->register_addr_len,
			       .speed = self->i2c_speed | 0x80},
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = {0};

	g_return_val_if_fail(data_sz <= 128, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(data_sz != 0, FALSE);

	/* get parent to issue command */
	parent = fu_rts54hid_module_get_parent(self, error);
	if (parent == NULL)
		return FALSE;

	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0x0, /* dst */
			    (const guint8 *)&cmd_buffer,
			    sizeof(cmd_buffer),
			    0x0, /* src */
			    sizeof(cmd_buffer),
			    error))
		return FALSE;
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    FU_RTS54HID_CMD_BUFFER_OFFSET_DATA, /* dst */
			    data,
			    data_sz,
			    0x0, /* src */
			    data_sz,
			    error))
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(parent),
				      0x0,
				      buf,
				      sizeof(buf),
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to write i2c @%04x: ", self->target_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_module_i2c_read(FuRts54HidModule *self,
			    guint32 cmd,
			    guint8 *data,
			    guint8 data_sz,
			    GError **error)
{
	FuRts54HidDevice *parent;
	const FuRts54HidCmdBuffer cmd_buffer = {
	    .cmd = FU_RTS54HID_CMD_WRITE_DATA,
	    .ext = FU_RTS54HID_EXT_I2C_READ,
	    .dwregaddr = GUINT32_TO_LE(cmd),
	    .bufferlen = GUINT16_TO_LE(data_sz),
	    .parameters_i2c = {.target_addr = self->target_addr,
			       .data_sz = self->register_addr_len,
			       .speed = self->i2c_speed | 0x80},
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = {0};

	g_return_val_if_fail(data_sz <= 192, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(data_sz != 0, FALSE);

	/* get parent to issue command */
	parent = fu_rts54hid_module_get_parent(self, error);
	if (parent == NULL)
		return FALSE;

	/* read from module */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0x0, /* dst */
			    (const guint8 *)&cmd_buffer,
			    sizeof(cmd_buffer),
			    0x0, /* src */
			    sizeof(cmd_buffer),
			    error))
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(parent),
				      0x0,
				      buf,
				      sizeof(buf),
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to write i2c @%04x: ", self->target_addr);
		return FALSE;
	}
	if (!fu_hid_device_get_report(FU_HID_DEVICE(parent),
				      0x0,
				      buf,
				      sizeof(buf),
				      FU_RTS54HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;
	return fu_memcpy_safe(data,
			      data_sz,
			      0x0,
			      buf,
			      sizeof(buf),
			      FU_RTS54HID_CMD_BUFFER_OFFSET_DATA,
			      data_sz,
			      error);
}

static gboolean
fu_rts54hid_module_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuRts54HidModule *self = FU_RTS54HID_MODULE(device);
	guint64 tmp = 0;

	/* load target address from quirks */
	if (g_strcmp0(key, "Rts54TargetAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->target_addr = tmp;
		return TRUE;
	}

	/* load i2c speed from quirks */
	if (g_strcmp0(key, "Rts54I2cSpeed") == 0) {
		if (!fu_strtoull(value,
				 &tmp,
				 0,
				 FU_RTS54HID_I2C_SPEED_LAST - 1,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
		self->i2c_speed = tmp;
		return TRUE;
	}

	/* load register address length from quirks */
	if (g_strcmp0(key, "Rts54RegisterAddrLen") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->register_addr_len = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static gboolean
fu_rts54hid_module_write_firmware(FuDevice *module,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuRts54HidModule *self = FU_RTS54HID_MODULE(module);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_RTS54HID_TRANSFER_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (0) {
		if (!fu_rts54hid_module_i2c_read(self, 0x0000, NULL, 0, error))
			return FALSE;
		if (!fu_rts54hid_module_i2c_write(self, NULL, 0, error))
			return FALSE;
	}

	/* write each block */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* write chunk */
		if (!fu_rts54hid_module_i2c_write(self,
						  fu_chunk_get_data(chk),
						  fu_chunk_get_data_sz(chk),
						  error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(progress,
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}

	/* success! */
	return TRUE;
}

static void
fu_rts54hid_module_init(FuRts54HidModule *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
}

static void
fu_rts54hid_module_class_init(FuRts54HidModuleClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_rts54hid_module_write_firmware;
	device_class->to_string = fu_rts54hid_module_to_string;
	device_class->set_quirk_kv = fu_rts54hid_module_set_quirk_kv;
}
