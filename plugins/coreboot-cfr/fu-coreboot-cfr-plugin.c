/*
 * Copyright 2026 Star Labs
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>

#include "fu-coreboot-cfr-plugin.h"
#include "fu-coreboot-cfr-setting.h"

#define COREBOOT_CFR_GUID		  "ceae4c1d-335b-4685-a4a0-fc4a94eea085"
#define COREBOOT_CFR_SETTINGS_VARIABLE	  "CorebootCfrSettings"
#define COREBOOT_CFR_SETTINGS_MAGIC	  0x44574643 /* CFWD */
#define COREBOOT_CFR_SETTINGS_VERSION	  2
#define COREBOOT_CFR_FLAG_READONLY	  (1u << 0)
#define COREBOOT_CFR_TYPE_ENUM		  1
#define COREBOOT_CFR_TYPE_NUMBER	  2
#define COREBOOT_CFR_TYPE_BOOL		  3
#define COREBOOT_CFR_APPLY_METHOD_NONE	  0
#define COREBOOT_CFR_APPLY_METHOD_APM_CNT 1
#define COREBOOT_CFR_SETTINGS_HDR_SZ	  16
#define COREBOOT_CFR_OPTION_RECORD_SZ	  60
#define COREBOOT_CFR_ENUM_RECORD_SZ	  8
#define COREBOOT_CFR_PENDING_REBOOT_PATH  "/run/fwupd/coreboot-cfr-pending-reboot"

typedef struct {
	guint32 magic;
	guint16 version;
	guint16 header_size;
	guint32 size;
	guint32 record_count;
} FuCorebootCfrSettingsHeader;

typedef struct {
	guint16 type;
	guint16 header_size;
	guint32 cfr_flags;
	guint64 object_id;
	guint32 runtime_apply_method;
	guint32 runtime_apply_id;
	guint32 default_value;
	guint32 min;
	guint32 max;
	guint32 step;
	guint32 display_flags;
	guint32 name_size;
	guint32 ui_name_size;
	guint32 help_text_size;
	guint32 enum_count;
} FuCorebootCfrOptionRecord;

typedef struct {
	guint32 value;
	guint32 ui_name_size;
} FuCorebootCfrEnumRecord;

struct _FuCorebootCfrPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCorebootCfrPlugin, fu_coreboot_cfr_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_coreboot_cfr_plugin_read_uint16(const guint8 *buf,
				   gsize bufsz,
				   gsize *offset,
				   guint16 *value,
				   GError **error)
{
	if (!fu_memread_uint16_safe(buf, bufsz, *offset, value, G_LITTLE_ENDIAN, error))
		return FALSE;
	*offset += sizeof(guint16);
	return TRUE;
}

static gboolean
fu_coreboot_cfr_plugin_read_uint32(const guint8 *buf,
				   gsize bufsz,
				   gsize *offset,
				   guint32 *value,
				   GError **error)
{
	if (!fu_memread_uint32_safe(buf, bufsz, *offset, value, G_LITTLE_ENDIAN, error))
		return FALSE;
	*offset += sizeof(guint32);
	return TRUE;
}

static gboolean
fu_coreboot_cfr_plugin_read_uint64(const guint8 *buf,
				   gsize bufsz,
				   gsize *offset,
				   guint64 *value,
				   GError **error)
{
	if (!fu_memread_uint64_safe(buf, bufsz, *offset, value, G_LITTLE_ENDIAN, error))
		return FALSE;
	*offset += sizeof(guint64);
	return TRUE;
}

static gchar *
fu_coreboot_cfr_plugin_read_string(const guint8 *buf,
				   gsize bufsz,
				   gsize *offset,
				   guint32 sz,
				   GError **error)
{
	gchar *str = NULL;

	if (sz == 0)
		return g_strdup("");

	if (*offset > bufsz || sz > bufsz - *offset) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata is truncated");
		return NULL;
	}

	if (buf[*offset + sz - 1] == '\0') {
		str = g_strndup((const gchar *)buf + *offset, sz - 1);
		*offset += sz;
		return str;
	}

	str = g_strndup((const gchar *)buf + *offset, sz);
	*offset += sz;
	return str;
}

static FwupdBiosSettingKind
fu_coreboot_cfr_plugin_setting_kind(guint16 type)
{
	if (type == COREBOOT_CFR_TYPE_NUMBER)
		return FWUPD_BIOS_SETTING_KIND_INTEGER;
	return FWUPD_BIOS_SETTING_KIND_ENUMERATION;
}

static gboolean
fu_coreboot_cfr_plugin_add_bool_values(GPtrArray *possible_values,
				       GHashTable *value_map,
				       GHashTable *reverse_value_map)
{
	g_ptr_array_add(possible_values, g_strdup("Disabled"));
	g_ptr_array_add(possible_values, g_strdup("Enabled"));
	g_hash_table_insert(value_map, g_strdup("Disabled"), g_strdup("0"));
	g_hash_table_insert(value_map, g_strdup("Enabled"), g_strdup("1"));
	g_hash_table_insert(reverse_value_map, g_strdup("0"), g_strdup("Disabled"));
	g_hash_table_insert(reverse_value_map, g_strdup("1"), g_strdup("Enabled"));
	return TRUE;
}

static const gchar *
fu_coreboot_cfr_plugin_get_pending_reboot_path(void)
{
	const gchar *path = g_getenv("FWUPD_COREBOOT_CFR_PENDING_REBOOT_PATH");
	if (path != NULL && path[0] != '\0')
		return path;
	return COREBOOT_CFR_PENDING_REBOOT_PATH;
}

static gboolean
fu_coreboot_cfr_plugin_register_pending_reboot(FuBiosSettings *bios_settings, GError **error)
{
	const gchar *path = fu_coreboot_cfr_plugin_get_pending_reboot_path();
	g_autofree gchar *dir = g_path_get_dirname(path);
	g_autofree gchar *value = NULL;
	g_autoptr(FwupdBiosSetting) setting = NULL;

	if (fu_bios_settings_get_attr(bios_settings, "org.coreboot.cfr.pending_reboot") != NULL)
		return TRUE;

	if (g_mkdir_with_parents(dir, 0755) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to create %s: %s",
			    dir,
			    fwupd_strerror(errno));
		return FALSE;
	}
	if (!g_file_test(path, G_FILE_TEST_EXISTS) && !g_file_set_contents(path, "0", -1, error)) {
		g_prefix_error(error, "failed to create %s: ", path);
		return FALSE;
	}
	if (!g_file_get_contents(path, &value, NULL, error)) {
		g_prefix_error(error, "failed to read %s: ", path);
		return FALSE;
	}
	g_strchomp(value);

	setting = fwupd_bios_setting_new(FWUPD_BIOS_SETTING_PENDING_REBOOT, path);
	fwupd_bios_setting_set_id(setting, "org.coreboot.cfr.pending_reboot");
	fwupd_bios_setting_set_description(setting, "Settings will apply after system reboots");
	fwupd_bios_setting_set_current_value(setting, value);
	fwupd_bios_setting_set_read_only(setting, TRUE);

	return fu_bios_settings_register_attr(bios_settings, setting, error);
}

static gboolean
fu_coreboot_cfr_plugin_read_header(const guint8 *buf,
				   gsize bufsz,
				   gsize *offset,
				   FuCorebootCfrSettingsHeader *hdr,
				   GError **error)
{
	return fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &hdr->magic, error) &&
	       fu_coreboot_cfr_plugin_read_uint16(buf, bufsz, offset, &hdr->version, error) &&
	       fu_coreboot_cfr_plugin_read_uint16(buf, bufsz, offset, &hdr->header_size, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &hdr->size, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &hdr->record_count, error);
}

static gboolean
fu_coreboot_cfr_plugin_read_option_record(const guint8 *buf,
					  gsize bufsz,
					  gsize *offset,
					  FuCorebootCfrOptionRecord *rec,
					  GError **error)
{
	return fu_coreboot_cfr_plugin_read_uint16(buf, bufsz, offset, &rec->type, error) &&
	       fu_coreboot_cfr_plugin_read_uint16(buf, bufsz, offset, &rec->header_size, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->cfr_flags, error) &&
	       fu_coreboot_cfr_plugin_read_uint64(buf, bufsz, offset, &rec->object_id, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf,
						  bufsz,
						  offset,
						  &rec->runtime_apply_method,
						  error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf,
						  bufsz,
						  offset,
						  &rec->runtime_apply_id,
						  error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->default_value, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->min, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->max, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->step, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->display_flags, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->name_size, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->ui_name_size, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf,
						  bufsz,
						  offset,
						  &rec->help_text_size,
						  error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->enum_count, error);
}

static gboolean
fu_coreboot_cfr_plugin_read_enum_record(const guint8 *buf,
					gsize bufsz,
					gsize *offset,
					FuCorebootCfrEnumRecord *rec,
					GError **error)
{
	return fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->value, error) &&
	       fu_coreboot_cfr_plugin_read_uint32(buf, bufsz, offset, &rec->ui_name_size, error);
}

static gboolean
fu_coreboot_cfr_plugin_parse_option(FuEfivars *efivars,
				    FuBiosSettings *bios_settings,
				    const guint8 *buf,
				    gsize bufsz,
				    gsize *offset,
				    GError **error)
{
	FuCorebootCfrOptionRecord rec;
	FuEfiVariableAttrs attrs = 0;
	g_autofree gchar *name = NULL;
	g_autofree gchar *ui_name = NULL;
	g_autofree gchar *help_text = NULL;
	g_autoptr(FwupdBiosSetting) setting = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) reverse_value_map =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_autoptr(GHashTable) value_map =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_autoptr(GPtrArray) possible_values = g_ptr_array_new_with_free_func(g_free);

	if (!fu_coreboot_cfr_plugin_read_option_record(buf, bufsz, offset, &rec, error))
		return FALSE;

	if (rec.header_size != COREBOOT_CFR_OPTION_RECORD_SZ) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR option record has unsupported size");
		return FALSE;
	}

	name = fu_coreboot_cfr_plugin_read_string(buf, bufsz, offset, rec.name_size, error);
	if (name == NULL)
		return FALSE;
	ui_name = fu_coreboot_cfr_plugin_read_string(buf, bufsz, offset, rec.ui_name_size, error);
	if (ui_name == NULL)
		return FALSE;
	help_text =
	    fu_coreboot_cfr_plugin_read_string(buf, bufsz, offset, rec.help_text_size, error);
	if (help_text == NULL)
		return FALSE;

	for (guint32 i = 0; i < rec.enum_count; i++) {
		FuCorebootCfrEnumRecord enum_rec;
		g_autofree gchar *enum_label = NULL;
		g_autofree gchar *value = NULL;

		if (!fu_coreboot_cfr_plugin_read_enum_record(buf, bufsz, offset, &enum_rec, error))
			return FALSE;
		enum_label = fu_coreboot_cfr_plugin_read_string(buf,
								bufsz,
								offset,
								enum_rec.ui_name_size,
								error);
		if (enum_label == NULL)
			return FALSE;

		value = g_strdup_printf("%u", enum_rec.value);
		g_ptr_array_add(possible_values, g_strdup(enum_label));
		g_hash_table_insert(value_map, g_strdup(enum_label), g_strdup(value));
		g_hash_table_insert(reverse_value_map,
				    g_strdup(value),
				    g_steal_pointer(&enum_label));
	}

	if (rec.type == COREBOOT_CFR_TYPE_BOOL)
		fu_coreboot_cfr_plugin_add_bool_values(possible_values,
						       value_map,
						       reverse_value_map);

	if (rec.runtime_apply_method != COREBOOT_CFR_APPLY_METHOD_NONE &&
	    rec.runtime_apply_method != COREBOOT_CFR_APPLY_METHOD_APM_CNT) {
		g_debug("skipping CFR option %s with unsupported runtime apply method 0x%x",
			name,
			rec.runtime_apply_method);
		return TRUE;
	}
	if (rec.runtime_apply_method == COREBOOT_CFR_APPLY_METHOD_APM_CNT &&
	    rec.runtime_apply_id == 0) {
		g_debug("skipping CFR option %s with invalid runtime apply ID", name);
		return TRUE;
	}

	if (!fu_efivars_get_attrs(efivars, COREBOOT_CFR_GUID, name, &attrs, error)) {
		g_prefix_error(error, "failed to get attributes for %s: ", name);
		return FALSE;
	}

	setting =
	    fu_coreboot_cfr_setting_new(efivars,
					name,
					ui_name,
					help_text,
					fu_coreboot_cfr_plugin_setting_kind(rec.type),
					attrs,
					rec.runtime_apply_method,
					rec.runtime_apply_id,
					rec.min,
					rec.max,
					rec.step,
					possible_values,
					value_map,
					reverse_value_map,
					rec.runtime_apply_method == COREBOOT_CFR_APPLY_METHOD_NONE
					    ? fu_coreboot_cfr_plugin_get_pending_reboot_path()
					    : NULL,
					error);
	if (setting == NULL)
		return FALSE;
	fwupd_bios_setting_set_read_only(setting, (rec.cfr_flags & COREBOOT_CFR_FLAG_READONLY) > 0);

	if (!fu_bios_settings_register_attr(bios_settings, setting, &error_local)) {
		g_debug("failed to register CFR option %s: %s", name, error_local->message);
		return TRUE;
	}

	g_debug("registered CFR option %s", name);
	return TRUE;
}

static gboolean
fu_coreboot_cfr_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	FuCorebootCfrSettingsHeader hdr;
	gsize bufsz = 0;
	gsize offset = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuBiosSettings) bios_settings = NULL;

	if (!fu_efivars_exists(efivars, COREBOOT_CFR_GUID, COREBOOT_CFR_SETTINGS_VARIABLE)) {
		g_debug("CFR settings metadata not found");
		return TRUE;
	}

	if (!fu_efivars_get_data(efivars,
				 COREBOOT_CFR_GUID,
				 COREBOOT_CFR_SETTINGS_VARIABLE,
				 &buf,
				 &bufsz,
				 NULL,
				 error)) {
		g_prefix_error_literal(error, "failed to read CFR settings metadata: ");
		return FALSE;
	}

	if (!fu_coreboot_cfr_plugin_read_header(buf, bufsz, &offset, &hdr, error))
		return FALSE;

	if (hdr.magic != COREBOOT_CFR_SETTINGS_MAGIC ||
	    hdr.version != COREBOOT_CFR_SETTINGS_VERSION ||
	    hdr.header_size != COREBOOT_CFR_SETTINGS_HDR_SZ || hdr.size != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata has an unsupported header");
		return FALSE;
	}

	bios_settings = fu_context_get_bios_settings(ctx);
	if (bios_settings == NULL)
		return TRUE;
	if (!fu_coreboot_cfr_plugin_register_pending_reboot(bios_settings, error))
		return FALSE;

	for (guint32 i = 0; i < hdr.record_count; i++) {
		if (!fu_coreboot_cfr_plugin_parse_option(efivars,
							 bios_settings,
							 buf,
							 bufsz,
							 &offset,
							 error))
			return FALSE;
	}

	if (offset != bufsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CFR settings metadata has trailing data");
		return FALSE;
	}

	return TRUE;
}

static void
fu_coreboot_cfr_plugin_class_init(FuCorebootCfrPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->coldplug = fu_coreboot_cfr_plugin_coldplug;
}

static void
fu_coreboot_cfr_plugin_init(FuCorebootCfrPlugin *self)
{
}
