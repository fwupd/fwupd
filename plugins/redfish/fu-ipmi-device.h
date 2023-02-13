/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_IPMI_DEVICE (fu_ipmi_device_get_type())
G_DECLARE_FINAL_TYPE(FuIpmiDevice, fu_ipmi_device, FU, IPMI_DEVICE, FuUdevDevice)

FuIpmiDevice *
fu_ipmi_device_new(FuContext *ctx);
gchar *
fu_ipmi_device_get_user_password(FuIpmiDevice *self, guint8 user_id, GError **error);
gboolean
fu_ipmi_device_set_user_name(FuIpmiDevice *self,
			     guint8 user_id,
			     const gchar *username,
			     GError **error);
gboolean
fu_ipmi_device_set_user_password(FuIpmiDevice *self,
				 guint8 user_id,
				 const gchar *password,
				 GError **error);
gboolean
fu_ipmi_device_set_user_enable(FuIpmiDevice *self, guint8 user_id, gboolean value, GError **error);
gboolean
fu_ipmi_device_set_user_priv(FuIpmiDevice *self,
			     guint8 user_id,
			     guint8 priv_limit,
			     guint8 channel,
			     GError **error);
