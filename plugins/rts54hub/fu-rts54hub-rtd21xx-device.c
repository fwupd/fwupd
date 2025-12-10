/*
 * Copyright 2021 Realtek Corporation
 * Copyright 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-rtd21xx-device.h"
#include "fu-rts54hub-struct.h"

typedef struct {
	guint8 target_addr;
	guint8 i2c_speed;
	guint8 register_addr_len;
} FuRts54hubRtd21xxDevicePrivate;

#define FU_RTS54HUB_DDCCI_BUFFER_MAXSZ 256

G_DEFINE_TYPE_WITH_PRIVATE(FuRts54hubRtd21xxDevice, fu_rts54hub_rtd21xx_device, FU_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_rts54hub_rtd21xx_device_get_instance_private(o))

static void
fu_rts54hub_rtd21xx_device_to_string(FuDevice *module, guint idt, GString *str)
{
	FuRts54hubRtd21xxDevice *self = FU_RTS54HUB_RTD21XX_DEVICE(module);
	FuRts54hubRtd21xxDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "TargetAddr", priv->target_addr);
	fwupd_codec_string_append_hex(str, idt, "I2cSpeed", priv->i2c_speed);
	fwupd_codec_string_append_hex(str, idt, "RegisterAddrLen", priv->register_addr_len);
}

static gboolean
fu_rts54hub_rtd21xx_device_set_quirk_kv(FuDevice *device,
					const gchar *key,
					const gchar *value,
					GError **error)
{
	FuRts54hubRtd21xxDevice *self = FU_RTS54HUB_RTD21XX_DEVICE(device);
	FuRts54hubRtd21xxDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp = 0;

	/* load target address from quirks */
	if (g_strcmp0(key, "Rts54TargetAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->target_addr = tmp;
		return TRUE;
	}

	/* load i2c speed from quirks */
	if (g_strcmp0(key, "Rts54I2cSpeed") == 0) {
		if (!fu_strtoull(value,
				 &tmp,
				 0,
				 FU_RTS54HUB_I2C_SPEED_LAST - 1,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
		priv->i2c_speed = tmp;
		return TRUE;
	}

	/* load register address length from quirks */
	if (g_strcmp0(key, "Rts54RegisterAddrLen") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->register_addr_len = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

gboolean
fu_rts54hub_rtd21xx_device_i2c_write(FuRts54hubRtd21xxDevice *self,
				     guint8 target_addr,
				     guint8 sub_addr,
				     const guint8 *data,
				     gsize datasz,
				     GError **error)
{
	FuRts54hubDevice *parent;
	FuRts54hubRtd21xxDevicePrivate *priv = GET_PRIVATE(self);

	parent = FU_RTS54HUB_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;
	if (!fu_rts54hub_device_vendor_cmd(parent, FU_RTS54HUB_VENDOR_CMD_ENABLE, error))
		return FALSE;

	if (target_addr != priv->target_addr) {
		if (!fu_rts54hub_device_i2c_config(parent,
						   target_addr,
						   1,
						   FU_RTS54HUB_I2C_SPEED_200K,
						   error))
			return FALSE;
		priv->target_addr = target_addr;
	}
	if (!fu_rts54hub_device_i2c_write(parent, sub_addr, data, datasz, error)) {
		g_prefix_error(error, "failed to write I2C @0x%02x:%02x: ", target_addr, sub_addr);
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), I2C_DELAY_AFTER_SEND);
	return TRUE;
}

static guint8
_fu_xor8(const guint8 *buf, gsize bufsz)
{
	guint8 tmp = 0;
	for (guint i = 0; i < bufsz; i++)
		tmp ^= buf[i];
	return tmp;
}

gboolean
fu_rts54hub_rtd21xx_device_ddcci_write(FuRts54hubRtd21xxDevice *self,
				       guint8 target_addr,
				       guint8 sub_addr,
				       const guint8 *data,
				       gsize datasz,
				       GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (datasz > FU_RTS54HUB_DDCCI_BUFFER_MAXSZ) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "DDC/CI write length exceed max length: ");
		return FALSE;
	}

	fu_byte_array_append_uint8(buf, target_addr);
	fu_byte_array_append_uint8(buf, sub_addr);
	fu_byte_array_append_uint8(buf, (guint8)datasz | 0x80);
	g_byte_array_append(buf, data, datasz);
	fu_byte_array_append_uint8(buf, _fu_xor8(buf->data, buf->len));

	if (!fu_rts54hub_rtd21xx_device_i2c_write(self,
						  target_addr,
						  sub_addr,
						  buf->data + 2,
						  buf->len - 2,
						  error)) {
		g_prefix_error_literal(error, "failed to DDC/CI write: ");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_rts54hub_rtd21xx_device_i2c_read(FuRts54hubRtd21xxDevice *self,
				    guint8 target_addr,
				    guint8 sub_addr,
				    guint8 *data,
				    gsize datasz,
				    GError **error)
{
	FuRts54hubDevice *parent;
	FuRts54hubRtd21xxDevicePrivate *priv = GET_PRIVATE(self);

	parent = FU_RTS54HUB_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;
	if (!fu_rts54hub_device_vendor_cmd(parent, FU_RTS54HUB_VENDOR_CMD_ENABLE, error))
		return FALSE;
	if (target_addr != priv->target_addr) {
		if (!fu_rts54hub_device_i2c_config(parent,
						   target_addr,
						   1,
						   FU_RTS54HUB_I2C_SPEED_200K,
						   error))
			return FALSE;
		priv->target_addr = target_addr;
	}
	if (!fu_rts54hub_device_i2c_read(parent, sub_addr, data, datasz, error)) {
		g_prefix_error_literal(error, "failed to read I2C: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_rts54hub_rtd21xx_device_ddcci_read(FuRts54hubRtd21xxDevice *self,
				      guint8 target_addr,
				      guint8 sub_addr,
				      guint8 *data,
				      gsize datasz,
				      GError **error)
{
	guint8 checksum = 0x50;
	guint8 buf[FU_RTS54HUB_DDCCI_BUFFER_MAXSZ] = {0x00};
	gsize length;

	if (datasz > FU_RTS54HUB_DDCCI_BUFFER_MAXSZ) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "DDC/CI read length exceed max length: ");
		return FALSE;
	}

	if (!fu_rts54hub_rtd21xx_device_i2c_read(self, target_addr, sub_addr, buf, datasz, error)) {
		g_prefix_error_literal(error, "failed to DDC/CI read I2C: ");
		return FALSE;
	}

	if (buf[0] != target_addr) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to DDC/CI read I2C target addr invalid: ");
		return FALSE;
	}

	length = buf[1] & 0x7F;
	if (length + 3 > sizeof(buf)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "DDC/CI read cmd length exceed max length: ");
		return FALSE;
	}

	/* verify checksum */
	checksum = 0x50 ^ _fu_xor8(buf, length + 2);
	if (checksum != buf[length + 2]) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to DDCCI read I2C checksum error: ");
		return FALSE;
	}

	/* success */
	return fu_memcpy_safe(data,
			      datasz,
			      0x0, /* dst */
			      buf,
			      sizeof(buf),
			      0x0, /* src */
			      length + 3,
			      error);
}

gboolean
fu_rts54hub_rtd21xx_device_read_status_raw(FuRts54hubRtd21xxDevice *self,
					   guint8 *status,
					   GError **error)
{
	guint8 buf = 0x00;
	if (!fu_rts54hub_rtd21xx_device_i2c_read(self,
						 UC_ISP_TARGET_ADDR,
						 UC_FOREGROUND_STATUS,
						 &buf,
						 sizeof(buf),
						 error))
		return FALSE;
	if (status != NULL)
		*status = buf;
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_device_read_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRts54hubRtd21xxDevice *self = FU_RTS54HUB_RTD21XX_DEVICE(device);
	guint8 status = 0xfd;
	if (!fu_rts54hub_rtd21xx_device_read_status_raw(self, &status, error))
		return FALSE;
	if (status == FU_RTS54HUB_RTD21XX_ISP_STATUS_BUSY) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "status was 0x%02x", status);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_rts54hub_rtd21xx_device_read_status(FuRts54hubRtd21xxDevice *self,
				       guint8 *status,
				       GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_rts54hub_rtd21xx_device_read_status_cb,
			       4200,
			       status,
			       error);
}

static void
fu_rts54hub_rtd21xx_device_init(FuRts54hubRtd21xxDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.rts54.i2c");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_install_duration(FU_DEVICE(self), 100); /* seconds */
	fu_device_set_logical_id(FU_DEVICE(self), "I2C");
	fu_device_retry_set_delay(FU_DEVICE(self), 30); /* ms */
}

static void
fu_rts54hub_rtd21xx_device_class_init(FuRts54hubRtd21xxDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_rts54hub_rtd21xx_device_to_string;
	device_class->set_quirk_kv = fu_rts54hub_rtd21xx_device_set_quirk_kv;
}
