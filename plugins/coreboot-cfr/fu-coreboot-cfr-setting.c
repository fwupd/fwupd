/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-coreboot-cfr-setting.h"

#define FU_COREBOOT_CFR_GUID	   "ceae4c1d-335b-4685-a4a0-fc4a94eea085"
#define FU_COREBOOT_CFR_DEVICE_FILE   "/dev/port"
#define FU_COREBOOT_CFR_APM_CNT_PORT  0xb2
#define FU_COREBOOT_CFR_APM_STS_PORT  0xb3
#define FU_COREBOOT_CFR_APM_APPLY_CMD 0xe3

struct _FuCorebootCfrSetting {
	FwupdBiosSetting parent_instance;
	FuContext *ctx;
	FuEfiVariableAttrs attrs;
	FuCorebootCfrApplyMethod runtime_apply_method;
	guint32 runtime_apply_id;
};

G_DEFINE_TYPE(FuCorebootCfrSetting, fu_coreboot_cfr_setting, FWUPD_TYPE_BIOS_SETTING)

static const gchar *
fu_coreboot_cfr_setting_get_id_naked(FuCorebootCfrSetting *self)
{
	const gchar *id = fwupd_bios_setting_get_id(FWUPD_BIOS_SETTING(self));
	if (g_str_has_prefix(id, "org.coreboot.cfr."))
		id += 17;
	return id;
}

static gboolean
fu_coreboot_cfr_setting_value_from_display(FuCorebootCfrSetting *self,
					   const gchar *value,
					   guint32 *value_u32,
					   GError **error)
{
	guint64 tmp = 0;
	if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	*value_u32 = (guint32)tmp;
	return TRUE;
}

static gboolean
fu_coreboot_cfr_setting_apply_apm_cnt(FuCorebootCfrSetting *self, guint32 value, GError **error)
{
	guint8 cmd = FU_COREBOOT_CFR_APM_APPLY_CMD;
	guint8 runtime_apply_id = (guint8)self->runtime_apply_id;
	guint8 status = 0;
	g_autoptr(FuUdevDevice) udev_device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	if (self->runtime_apply_id == 0)
		return TRUE;
	if (self->runtime_apply_id > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid runtime apply ID 0x%x",
			    self->runtime_apply_id);
		return FALSE;
	}

	udev_device = g_object_new(FU_TYPE_UDEV_DEVICE, "context", self->ctx, NULL);
	fu_udev_device_add_open_flag(udev_device, FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(udev_device, FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_set_device_file(udev_device, FU_COREBOOT_CFR_DEVICE_FILE);
	locker = fu_device_locker_new(FU_DEVICE(udev_device), error);
	if (locker == NULL)
		return FALSE;
	if (!fu_udev_device_pwrite(udev_device,
				   FU_COREBOOT_CFR_APM_STS_PORT,
				   &runtime_apply_id,
				   sizeof(runtime_apply_id),
				   error)) {
		g_prefix_error_literal(error, "failed to apply at runtime: ");
		return FALSE;
	}
	if (!fu_udev_device_pwrite(udev_device,
				   FU_COREBOOT_CFR_APM_CNT_PORT,
				   &cmd,
				   sizeof(cmd),
				   error)) {
		g_prefix_error_literal(error, "failed to apply at runtime: ");
		return FALSE;
	}
	if (!fu_udev_device_pread(udev_device,
				  FU_COREBOOT_CFR_APM_STS_PORT,
				  &status,
				  sizeof(status),
				  error)) {
		g_prefix_error_literal(error, "failed to apply at runtime: ");
		return FALSE;
	}
	if (status != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to apply at runtime: SMI returned 0x%02x",
			    status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coreboot_cfr_setting_apply(FuCorebootCfrSetting *self, guint32 value, GError **error)
{
	if (self->runtime_apply_method == FU_COREBOOT_CFR_APPLY_METHOD_NONE)
		return TRUE;
	if (self->runtime_apply_method == FU_COREBOOT_CFR_APPLY_METHOD_APM_CNT)
		return fu_coreboot_cfr_setting_apply_apm_cnt(self, value, error);
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "unsupported runtime apply method 0x%x",
		    self->runtime_apply_method);
	return FALSE;
}

static gboolean
fu_coreboot_cfr_setting_read_current(FuCorebootCfrSetting *self, guint32 *value, GError **error)
{
	const gchar *id = fu_coreboot_cfr_setting_get_id_naked(self);
	gsize data_sz = 0;
	g_autofree guint8 *data = NULL;

	if (!fu_efivars_get_data(fu_context_get_efivars(self->ctx),
				 FU_COREBOOT_CFR_GUID,
				 id,
				 &data,
				 &data_sz,
				 NULL,
				 error))
		return FALSE;
	if (data_sz != 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid size 0x%x",
			    (guint)data_sz);
		return FALSE;
	}
	return fu_memread_uint32_safe(data, data_sz, 0x0, value, G_LITTLE_ENDIAN, error);
}

static gboolean
fu_coreboot_cfr_setting_write_value(FwupdBiosSetting *setting, const gchar *value, GError **error)
{
	FuCorebootCfrSetting *self = FU_COREBOOT_CFR_SETTING(setting);
	const gchar *id = fu_coreboot_cfr_setting_get_id_naked(self);
	guint32 old_value = 0;
	guint8 old_value_buf[4] = {0};
	guint32 value_u32 = 0;
	guint8 value_buf[4] = {0};
	g_autoptr(GError) error_local = NULL;

	if (!fu_coreboot_cfr_setting_value_from_display(self, value, &value_u32, error))
		return FALSE;
	if (!fu_coreboot_cfr_setting_read_current(self, &old_value, error))
		return FALSE;
	if (!fu_memwrite_uint32_safe(value_buf,
				     sizeof(value_buf),
				     0x0,
				     value_u32,
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;
	if (!fu_efivars_set_data(fu_context_get_efivars(self->ctx),
				 FU_COREBOOT_CFR_GUID,
				 id,
				 value_buf,
				 sizeof(value_buf),
				 self->attrs,
				 error))
		return FALSE;
	if (!fu_coreboot_cfr_setting_apply(self, value_u32, &error_local)) {
		if (!fu_memwrite_uint32_safe(old_value_buf,
					     sizeof(old_value_buf),
					     0x0,
					     old_value,
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;
		if (!fu_efivars_set_data(fu_context_get_efivars(self->ctx),
					 FU_COREBOOT_CFR_GUID,
					 id,
					 old_value_buf,
					 sizeof(old_value_buf),
					 self->attrs,
					 error)) {
			g_prefix_error(error,
				       "failed to restore after apply failure: %s: ",
				       error_local->message);
			return FALSE;
		}

		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* success */
	return TRUE;
}

void
fu_coreboot_cfr_setting_set_id(FuCorebootCfrSetting *self, const gchar *id)
{
	g_autofree gchar *id_prefixed = g_strdup_printf("org.coreboot.cfr.%s", id);
	fwupd_bios_setting_set_id(FWUPD_BIOS_SETTING(self), id_prefixed);
}

static gchar *
fu_coreboot_cfr_setting_read_value(FwupdBiosSetting *setting, GError **error)
{
	FuCorebootCfrSetting *self = FU_COREBOOT_CFR_SETTING(setting);
	const gchar *id = fu_coreboot_cfr_setting_get_id_naked(self);
	guint32 current = 0;

	if (!fu_efivars_get_attrs(fu_context_get_efivars(self->ctx),
				  FU_COREBOOT_CFR_GUID,
				  id,
				  &self->attrs,
				  error)) {
		g_prefix_error_literal(error, "failed to get attributes: ");
		return NULL;
	}
	if (!fu_coreboot_cfr_setting_read_current(self, &current, error))
		return NULL;
	return g_strdup_printf("%u", current);
}

void
fu_coreboot_cfr_setting_set_runtime_apply_id(FuCorebootCfrSetting *self, guint32 runtime_apply_id)
{
	self->runtime_apply_id = runtime_apply_id;
}

static void
fu_coreboot_cfr_setting_init(FuCorebootCfrSetting *self)
{
}

static void
fu_coreboot_cfr_setting_finalize(GObject *obj)
{
	FuCorebootCfrSetting *self = FU_COREBOOT_CFR_SETTING(obj);
	g_clear_object(&self->ctx);
	G_OBJECT_CLASS(fu_coreboot_cfr_setting_parent_class)->finalize(obj);
}

static void
fu_coreboot_cfr_setting_class_init(FuCorebootCfrSettingClass *klass)
{
	FwupdBiosSettingClass *bios_setting_class = FWUPD_BIOS_SETTING_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	bios_setting_class->write_value = fu_coreboot_cfr_setting_write_value;
	bios_setting_class->read_value = fu_coreboot_cfr_setting_read_value;
	object_class->finalize = fu_coreboot_cfr_setting_finalize;
}

FuCorebootCfrSetting *
fu_coreboot_cfr_setting_new(FuContext *ctx, FuCorebootCfrApplyMethod runtime_apply_method)
{
	g_autoptr(FuCorebootCfrSetting) self = g_object_new(FU_TYPE_COREBOOT_CFR_SETTING, NULL);
	self->ctx = g_object_ref(ctx);
	self->runtime_apply_method = runtime_apply_method;
	return g_steal_pointer(&self);
}
