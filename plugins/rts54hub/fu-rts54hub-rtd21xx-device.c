/*
 * Copyright (C) 2021 Realtek Corporation
 * Copyright (C) 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-rtd21xx-device.h"

typedef struct {
	guint8 target_addr;
	guint8 i2c_speed;
	guint8 register_addr_len;
} FuRts54hubRtd21xxDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuRts54hubRtd21xxDevice, fu_rts54hub_rtd21xx_device, FU_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_rts54hub_rtd21xx_device_get_instance_private(o))

typedef enum {
	VENDOR_CMD_DISABLE = 0x00,
	VENDOR_CMD_ENABLE = 0x01,
	VENDOR_CMD_ACCESS_FLASH = 0x02,
} VendorCmd;

static void
fu_rts54hub_rtd21xx_device_to_string(FuDevice *module, guint idt, GString *str)
{
	FuRts54hubRtd21xxDevice *self = FU_RTS54HUB_RTD21XX_DEVICE(module);
	FuRts54hubRtd21xxDevicePrivate *priv = GET_PRIVATE(self);
	fu_common_string_append_kx(str, idt, "TargetAddr", priv->target_addr);
	fu_common_string_append_kx(str, idt, "I2cSpeed", priv->i2c_speed);
	fu_common_string_append_kx(str, idt, "RegisterAddrLen", priv->register_addr_len);
}

static FuRts54HubDevice *
fu_rts54hub_rtd21xx_device_get_parent(FuRts54hubRtd21xxDevice *self, GError **error)
{
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no parent set");
		return NULL;
	}
	return FU_RTS54HUB_DEVICE(parent);
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
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->target_addr = tmp;
		return TRUE;
	}

	/* load i2c speed from quirks */
	if (g_strcmp0(key, "Rts54I2cSpeed") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, FU_RTS54HUB_I2C_SPEED_LAST - 1, error))
			return FALSE;
		priv->i2c_speed = tmp;
		return TRUE;
	}

	/* load register address length from quirks */
	if (g_strcmp0(key, "Rts54RegisterAddrLen") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->register_addr_len = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
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
	FuRts54HubDevice *parent;
	FuRts54hubRtd21xxDevicePrivate *priv = GET_PRIVATE(self);

	parent = fu_rts54hub_rtd21xx_device_get_parent(self, error);
	if (parent == NULL)
		return FALSE;
	if (!fu_rts54hub_device_vendor_cmd(parent, VENDOR_CMD_ENABLE, error))
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
	g_usleep(I2C_DELAY_AFTER_SEND);
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
	FuRts54HubDevice *parent;
	FuRts54hubRtd21xxDevicePrivate *priv = GET_PRIVATE(self);

	parent = fu_rts54hub_rtd21xx_device_get_parent(self, error);
	if (parent == NULL)
		return FALSE;
	if (!fu_rts54hub_device_vendor_cmd(parent, VENDOR_CMD_ENABLE, error))
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
		g_prefix_error(error, "failed to read I2C: ");
		return FALSE;
	}
	return TRUE;
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
	if (status == ISP_STATUS_BUSY) {
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
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.rts54.i2c");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_install_duration(FU_DEVICE(self), 100); /* seconds */
	fu_device_set_logical_id(FU_DEVICE(self), "I2C");
	fu_device_retry_set_delay(FU_DEVICE(self), 30); /* ms */
}

static void
fu_rts54hub_rtd21xx_device_class_init(FuRts54hubRtd21xxDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_rts54hub_rtd21xx_device_to_string;
	klass_device->set_quirk_kv = fu_rts54hub_rtd21xx_device_set_quirk_kv;
}
