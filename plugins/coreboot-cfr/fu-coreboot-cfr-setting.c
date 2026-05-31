/*
 * Copyright 2026 Star Labs
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "fu-coreboot-cfr-setting.h"

#define COREBOOT_CFR_GUID		  "ceae4c1d-335b-4685-a4a0-fc4a94eea085"
#define COREBOOT_CFR_APPLY_DEVICE	  "/dev/port"
#define COREBOOT_CFR_APM_CNT_PORT	  0xb2
#define COREBOOT_CFR_APM_STS_PORT	  0xb3
#define COREBOOT_CFR_APM_APPLY_CMD	  0xe3
#define COREBOOT_CFR_APPLY_METHOD_NONE	  0
#define COREBOOT_CFR_APPLY_METHOD_APM_CNT 1

struct _FuCorebootCfrSetting {
	FwupdBiosSetting parent_instance;
	FuEfivars *efivars;
	FuEfiVariableAttrs attrs;
	GHashTable *reverse_value_map;
	GHashTable *value_map;
	gchar *name;
	gchar *pending_reboot_path;
	guint32 runtime_apply_method;
	guint32 runtime_apply_id;
};

G_DEFINE_TYPE(FuCorebootCfrSetting, fu_coreboot_cfr_setting, FWUPD_TYPE_BIOS_SETTING)

static gboolean
fu_coreboot_cfr_setting_read_current(FuCorebootCfrSetting *self, guint32 *value, GError **error);

static gboolean
fu_coreboot_cfr_setting_value_from_display(FuCorebootCfrSetting *self,
					   const gchar *value,
					   guint32 *value_u32,
					   GError **error)
{
	g_autofree gchar *value_raw = NULL;
	const gchar *mapped = NULL;
	guint64 tmp = 0;

	mapped = g_hash_table_lookup(self->value_map, value);
	if (mapped != NULL)
		value = mapped;

	if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
		return FALSE;

	if (mapped == NULL && g_hash_table_size(self->value_map) > 0) {
		value_raw = g_strdup_printf("%" G_GUINT64_FORMAT, tmp);
		if (!g_hash_table_contains(self->reverse_value_map, value_raw)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s is not a valid value for %s",
				    value,
				    self->name);
			return FALSE;
		}
	}

	*value_u32 = (guint32)tmp;
	return TRUE;
}

static gboolean
fu_coreboot_cfr_setting_port_write(gint fd, goffset port, guint8 value, GError **error)
{
	if (pwrite(fd, &value, sizeof(value), port) != sizeof(value)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to write I/O port 0x%x: %s",
			    (guint)port,
			    fwupd_strerror(errno));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_coreboot_cfr_setting_port_read(gint fd, goffset port, guint8 *value, GError **error)
{
	if (pread(fd, value, sizeof(*value), port) != sizeof(*value)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "failed to read I/O port 0x%x: %s",
			    (guint)port,
			    fwupd_strerror(errno));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_coreboot_cfr_setting_apply_apm_cnt(FuCorebootCfrSetting *self, guint32 value, GError **error)
{
	guint8 cmd = COREBOOT_CFR_APM_APPLY_CMD;
	guint8 runtime_apply_id = (guint8)self->runtime_apply_id;
	guint8 status = 0;
	g_autofd gint fd = -1;

	if (self->runtime_apply_id == 0)
		return TRUE;
	if (self->runtime_apply_id > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "%s has invalid runtime apply ID 0x%x",
			    self->name,
			    self->runtime_apply_id);
		return FALSE;
	}

	fd = open(COREBOOT_CFR_APPLY_DEVICE, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to open %s: %s",
			    COREBOOT_CFR_APPLY_DEVICE,
			    fwupd_strerror(errno));
		return FALSE;
	}

	if (!fu_coreboot_cfr_setting_port_write(fd,
						COREBOOT_CFR_APM_STS_PORT,
						runtime_apply_id,
						error) ||
	    !fu_coreboot_cfr_setting_port_write(fd, COREBOOT_CFR_APM_CNT_PORT, cmd, error) ||
	    !fu_coreboot_cfr_setting_port_read(fd, COREBOOT_CFR_APM_STS_PORT, &status, error)) {
		g_prefix_error(error, "failed to apply %s at runtime: ", self->name);
		return FALSE;
	}

	if (status != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to apply %s at runtime: SMI returned 0x%02x",
			    self->name,
			    status);
		return FALSE;
	}

	g_debug("applied %s=%u at runtime", self->name, value);
	return TRUE;
}

static gboolean
fu_coreboot_cfr_setting_mark_pending_reboot(FuCorebootCfrSetting *self, GError **error)
{
	if (self->pending_reboot_path == NULL)
		return TRUE;

	if (!g_file_set_contents(self->pending_reboot_path, "1", -1, error)) {
		g_prefix_error_literal(error, "failed to mark pending reboot: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_coreboot_cfr_setting_apply(FuCorebootCfrSetting *self, guint32 value, GError **error)
{
	switch (self->runtime_apply_method) {
	case COREBOOT_CFR_APPLY_METHOD_NONE:
		return fu_coreboot_cfr_setting_mark_pending_reboot(self, error);
	case COREBOOT_CFR_APPLY_METHOD_APM_CNT:
		return fu_coreboot_cfr_setting_apply_apm_cnt(self, value, error);
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s has unsupported runtime apply method 0x%x",
			    self->name,
			    self->runtime_apply_method);
		return FALSE;
	}
}

static gboolean
fu_coreboot_cfr_setting_write_value(FwupdBiosSetting *setting, const gchar *value, GError **error)
{
	FuCorebootCfrSetting *self = FU_COREBOOT_CFR_SETTING(setting);
	guint32 old_value = 0;
	guint8 old_value_buf[sizeof(guint32)] = {0};
	guint32 value_u32 = 0;
	guint8 value_buf[sizeof(guint32)] = {0};
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

	if (!fu_efivars_set_data(self->efivars,
				 COREBOOT_CFR_GUID,
				 self->name,
				 value_buf,
				 sizeof(value_buf),
				 self->attrs,
				 error)) {
		g_prefix_error(error, "failed to write %s: ", self->name);
		return FALSE;
	}

	if (!fu_coreboot_cfr_setting_apply(self, value_u32, &error_local)) {
		if (!fu_memwrite_uint32_safe(old_value_buf,
					     sizeof(old_value_buf),
					     0x0,
					     old_value,
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		if (!fu_efivars_set_data(self->efivars,
					 COREBOOT_CFR_GUID,
					 self->name,
					 old_value_buf,
					 sizeof(old_value_buf),
					 self->attrs,
					 error)) {
			g_prefix_error(error,
				       "failed to restore %s after apply failure: %s: ",
				       self->name,
				       error_local->message);
			return FALSE;
		}

		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	fwupd_bios_setting_set_current_value(setting, value);
	g_debug("set %s to %s", fwupd_bios_setting_get_id(setting), value);
	return TRUE;
}

static void
fu_coreboot_cfr_setting_finalize(GObject *obj)
{
	FuCorebootCfrSetting *self = FU_COREBOOT_CFR_SETTING(obj);

	g_clear_object(&self->efivars);
	g_clear_pointer(&self->reverse_value_map, g_hash_table_unref);
	g_clear_pointer(&self->value_map, g_hash_table_unref);
	g_free(self->name);
	g_free(self->pending_reboot_path);

	G_OBJECT_CLASS(fu_coreboot_cfr_setting_parent_class)->finalize(obj);
}

static void
fu_coreboot_cfr_setting_class_init(FuCorebootCfrSettingClass *klass)
{
	FwupdBiosSettingClass *bios_setting_class = FWUPD_BIOS_SETTING_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	bios_setting_class->write_value = fu_coreboot_cfr_setting_write_value;
	object_class->finalize = fu_coreboot_cfr_setting_finalize;
}

static void
fu_coreboot_cfr_setting_init(FuCorebootCfrSetting *self)
{
}

static gboolean
fu_coreboot_cfr_setting_read_current(FuCorebootCfrSetting *self, guint32 *value, GError **error)
{
	gsize data_sz = 0;
	g_autofree guint8 *data = NULL;

	if (!fu_efivars_get_data(self->efivars,
				 COREBOOT_CFR_GUID,
				 self->name,
				 &data,
				 &data_sz,
				 NULL,
				 error))
		return FALSE;

	if (data_sz != sizeof(guint32)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "%s has invalid size 0x%x",
			    self->name,
			    (guint)data_sz);
		return FALSE;
	}

	return fu_memread_uint32_safe(data, data_sz, 0x0, value, G_LITTLE_ENDIAN, error);
}

FwupdBiosSetting *
fu_coreboot_cfr_setting_new(FuEfivars *efivars,
			    const gchar *name,
			    const gchar *ui_name,
			    const gchar *description,
			    FwupdBiosSettingKind kind,
			    FuEfiVariableAttrs attrs,
			    guint32 runtime_apply_method,
			    guint32 runtime_apply_id,
			    guint32 lower_bound,
			    guint32 upper_bound,
			    guint32 scalar_increment,
			    GPtrArray *possible_values,
			    GHashTable *value_map,
			    GHashTable *reverse_value_map,
			    const gchar *pending_reboot_path,
			    GError **error)
{
	FuCorebootCfrSetting *setting = NULL;
	guint32 current = 0;
	g_autofree gchar *id = NULL;
	g_autofree gchar *current_raw = NULL;
	g_autoptr(FuCorebootCfrSetting) self = NULL;

	self = g_object_new(FU_TYPE_COREBOOT_CFR_SETTING, NULL);
	setting = FU_COREBOOT_CFR_SETTING(self);
	setting->efivars = g_object_ref(efivars);
	setting->attrs = attrs;
	setting->name = g_strdup(name);
	setting->runtime_apply_method = runtime_apply_method;
	setting->runtime_apply_id = runtime_apply_id;
	setting->value_map = g_hash_table_ref(value_map);
	setting->reverse_value_map = g_hash_table_ref(reverse_value_map);
	setting->pending_reboot_path = g_strdup(pending_reboot_path);

	id = g_strdup_printf("org.coreboot.cfr.%s", name);
	fwupd_bios_setting_set_id(FWUPD_BIOS_SETTING(self), id);
	fwupd_bios_setting_set_name(FWUPD_BIOS_SETTING(self), name);
	fwupd_bios_setting_set_description(FWUPD_BIOS_SETTING(self),
					   description[0] != '\0' ? description : ui_name);
	fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(self), kind);

	if (kind == FWUPD_BIOS_SETTING_KIND_INTEGER) {
		fwupd_bios_setting_set_lower_bound(FWUPD_BIOS_SETTING(self), lower_bound);
		fwupd_bios_setting_set_upper_bound(FWUPD_BIOS_SETTING(self), upper_bound);
		fwupd_bios_setting_set_scalar_increment(FWUPD_BIOS_SETTING(self), scalar_increment);
	}

	if (kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		for (guint i = 0; i < possible_values->len; i++) {
			const gchar *possible = g_ptr_array_index(possible_values, i);
			fwupd_bios_setting_add_possible_value(FWUPD_BIOS_SETTING(self), possible);
		}
	}

	if (!fu_coreboot_cfr_setting_read_current(setting, &current, error))
		return NULL;

	current_raw = g_strdup_printf("%u", current);
	if (kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		const gchar *display = g_hash_table_lookup(reverse_value_map, current_raw);
		fwupd_bios_setting_set_current_value(FWUPD_BIOS_SETTING(self),
						     display != NULL ? display : current_raw);
	} else {
		fwupd_bios_setting_set_current_value(FWUPD_BIOS_SETTING(self), current_raw);
	}

	return FWUPD_BIOS_SETTING(g_steal_pointer(&self));
}
