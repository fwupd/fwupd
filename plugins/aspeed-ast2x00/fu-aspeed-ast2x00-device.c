/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-aspeed-ast2x00-device.h"

typedef struct {
	FuAspeedAst2x00Revision revision;
} FuAspeedAst2x00DevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuAspeedAst2x00Device, fu_aspeed_ast2x00_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_aspeed_ast2x00_device_get_instance_private(o))

static void
fu_aspeed_ast2x00_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAspeedAst2x00Device *self = FU_ASPEED_AST2X00_DEVICE(device);
	FuAspeedAst2x00DevicePrivate *priv = GET_PRIVATE(self);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_aspeed_ast2x00_device_parent_class)->to_string(device, idt, str);

	fu_string_append_kx(str, idt, "Revision", priv->revision);
}

FuAspeedAst2x00Revision
fu_aspeed_ast2x00_device_get_revision(FuAspeedAst2x00Device *self)
{
	FuAspeedAst2x00DevicePrivate *priv = GET_PRIVATE(self);
	return priv->revision;
}

static gboolean
fu_aspeed_ast2x00_device_probe(FuDevice *device, GError **error)
{
	FuAspeedAst2x00Device *self = FU_ASPEED_AST2X00_DEVICE(device);
	FuAspeedAst2x00DevicePrivate *priv = GET_PRIVATE(self);
	FuContext *ctx = fu_device_get_context(device);
	GPtrArray *hwids = fu_context_get_hwid_guids(ctx);

	/* find revision using quirks */
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *hwid = g_ptr_array_index(hwids, i);
		fu_device_add_guid_full(device, hwid, FU_DEVICE_INSTANCE_FLAG_QUIRKS);
	}
	if (priv->revision == 0x0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "invalid AspeedAst2x00Revision");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_aspeed_ast2x00_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuAspeedAst2x00Device *self = FU_ASPEED_AST2X00_DEVICE(device);
	FuAspeedAst2x00DevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp = 0;

	if (g_strcmp0(key, "AspeedAst2x00Revision") == 0) {
		if (!fu_strtoull(value, &tmp, FU_ASPEED_AST2400, FU_ASPEED_AST2600, error))
			return FALSE;
		priv->revision = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_aspeed_ast2x00_device_init(FuAspeedAst2x00Device *self)
{
	fu_device_set_physical_id(FU_DEVICE(self), "/dev/mem");
	fu_device_set_name(FU_DEVICE(self), "AST2X00");
	fu_device_set_summary(FU_DEVICE(self), "BMC SoC");
	fu_device_set_vendor(FU_DEVICE(self), "ASPEED Technology");
	fu_device_add_vendor_id(FU_DEVICE(self), "PCI:0x1A03");
	fu_device_add_instance_id_full(FU_DEVICE(self), "cpu", FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), "computer");
}

static void
fu_aspeed_ast2x00_device_class_init(FuAspeedAst2x00DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_aspeed_ast2x00_device_to_string;
	klass_device->probe = fu_aspeed_ast2x00_device_probe;
	klass_device->set_quirk_kv = fu_aspeed_ast2x00_device_set_quirk_kv;
}
