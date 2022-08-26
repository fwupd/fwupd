/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBiosSettings"

#include "config.h"

#include <glib/gi18n.h>

#include "fwupd-bios-setting-private.h"
#include "fwupd-error.h"

#include "fu-bios-settings-private.h"
#include "fu-path.h"
#include "fu-string.h"

struct _FuBiosSettings {
	GObject parent_instance;
	GHashTable *descriptions;
	GHashTable *read_only;
	GPtrArray *attrs;
};

G_DEFINE_TYPE(FuBiosSettings, fu_bios_settings, G_TYPE_OBJECT)

static void
fu_bios_settings_finalize(GObject *obj)
{
	FuBiosSettings *self = FU_BIOS_SETTINGS(obj);
	g_ptr_array_unref(self->attrs);
	g_hash_table_unref(self->descriptions);
	g_hash_table_unref(self->read_only);
	G_OBJECT_CLASS(fu_bios_settings_parent_class)->finalize(obj);
}

static void
fu_bios_settings_class_init(FuBiosSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_bios_settings_finalize;
}

static gboolean
fu_bios_setting_get_key(FwupdBiosSetting *attr, const gchar *key, gchar **value_out, GError **error)
{
	g_autofree gchar *tmp = NULL;

	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(attr), FALSE);
	g_return_val_if_fail(&value_out != NULL, FALSE);

	tmp = g_build_filename(fwupd_bios_setting_get_path(attr), key, NULL);
	if (!g_file_get_contents(tmp, value_out, NULL, error)) {
		g_prefix_error(error, "failed to load %s: ", key);
		return FALSE;
	}
	g_strchomp(*value_out);
	return TRUE;
}

static gboolean
fu_bios_setting_set_description(FuBiosSettings *self, FwupdBiosSetting *attr, GError **error)
{
	g_autofree gchar *data = NULL;
	const gchar *value;

	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(attr), FALSE);

	/* Try ID, then name, and then key */
	value = g_hash_table_lookup(self->descriptions, fwupd_bios_setting_get_id(attr));
	if (value != NULL) {
		fwupd_bios_setting_set_description(attr, value);
		return TRUE;
	}
	value = g_hash_table_lookup(self->descriptions, fwupd_bios_setting_get_name(attr));
	if (value != NULL) {
		fwupd_bios_setting_set_description(attr, value);
		return TRUE;
	}
	if (!fu_bios_setting_get_key(attr, "display_name", &data, error))
		return FALSE;
	fwupd_bios_setting_set_description(attr, data);

	return TRUE;
}

static guint64
fu_bios_setting_get_key_as_integer(FwupdBiosSetting *attr, const gchar *key, GError **error)
{
	g_autofree gchar *str = NULL;
	guint64 tmp;

	if (!fu_bios_setting_get_key(attr, key, &str, error))
		return G_MAXUINT64;
	if (!fu_strtoull(str, &tmp, 0, G_MAXUINT64, error)) {
		g_prefix_error(error, "failed to convert %s to integer: ", key);
		return G_MAXUINT64;
	}
	return tmp;
}

static gboolean
fu_bios_setting_set_enumeration_attrs(FwupdBiosSetting *attr, GError **error)
{
	const gchar *delimiters[] = {",", ";", NULL};
	g_autofree gchar *str = NULL;

	if (!fu_bios_setting_get_key(attr, "possible_values", &str, error))
		return FALSE;
	for (guint j = 0; delimiters[j] != NULL; j++) {
		g_auto(GStrv) vals = NULL;
		if (g_strrstr(str, delimiters[j]) == NULL)
			continue;
		vals = fu_strsplit(str, strlen(str), delimiters[j], -1);
		if (vals[0] != NULL)
			fwupd_bios_setting_set_kind(attr, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
		for (guint i = 0; vals[i] != NULL && vals[i][0] != '\0'; i++)
			fwupd_bios_setting_add_possible_value(attr, vals[i]);
	}
	return TRUE;
}

static gboolean
fu_bios_setting_set_string_attrs(FwupdBiosSetting *attr, GError **error)
{
	guint64 tmp;

	tmp = fu_bios_setting_get_key_as_integer(attr, "min_length", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_setting_set_lower_bound(attr, tmp);
	tmp = fu_bios_setting_get_key_as_integer(attr, "max_length", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_setting_set_upper_bound(attr, tmp);
	fwupd_bios_setting_set_kind(attr, FWUPD_BIOS_SETTING_KIND_STRING);
	return TRUE;
}

static gboolean
fu_bios_setting_set_integer_attrs(FwupdBiosSetting *attr, GError **error)
{
	guint64 tmp;

	tmp = fu_bios_setting_get_key_as_integer(attr, "min_value", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_setting_set_lower_bound(attr, tmp);
	tmp = fu_bios_setting_get_key_as_integer(attr, "max_value", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_setting_set_upper_bound(attr, tmp);
	tmp = fu_bios_setting_get_key_as_integer(attr, "scalar_increment", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_setting_set_scalar_increment(attr, tmp);
	fwupd_bios_setting_set_kind(attr, FWUPD_BIOS_SETTING_KIND_INTEGER);
	return TRUE;
}

static gboolean
fu_bios_setting_set_current_value(FwupdBiosSetting *attr, GError **error)
{
	g_autofree gchar *str = NULL;

	if (!fu_bios_setting_get_key(attr, "current_value", &str, error))
		return FALSE;
	fwupd_bios_setting_set_current_value(attr, str);
	return TRUE;
}

#define LENOVO_POSSIBLE_NEEDLE	"[Optional:"
#define LENOVO_READ_ONLY_NEEDLE "[Status:ShowOnly]"
#define LENOVO_EXCLUDED		"[Excluded from boot order:"

static void
fu_bios_setting_set_read_only(FuBiosSettings *self, FwupdBiosSetting *attr)
{
	if (fwupd_bios_setting_get_kind(attr) == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		const gchar *value =
		    g_hash_table_lookup(self->read_only, fwupd_bios_setting_get_id(attr));
		if (g_strcmp0(value, fwupd_bios_setting_get_current_value(attr)) == 0)
			fwupd_bios_setting_set_read_only(attr, TRUE);
	}
}

static gboolean
fu_bios_setting_fixup_lenovo_thinklmi_bug(FwupdBiosSetting *attr, GError **error)
{
	const gchar *current_value = fwupd_bios_setting_get_current_value(attr);
	const gchar *tmp;
	g_autoptr(GString) str = NULL;
	g_autoptr(GString) right_str = NULL;
	g_auto(GStrv) vals = NULL;

	if (g_getenv("FWUPD_BIOS_SETTING_VERBOSE") != NULL) {
		g_debug("Processing %s: (%s)",
			fwupd_bios_setting_get_name(attr),
			fwupd_bios_setting_get_current_value(attr));
	}

	/* We have read only */
	tmp = g_strrstr(current_value, LENOVO_READ_ONLY_NEEDLE);
	if (tmp != NULL) {
		fwupd_bios_setting_set_read_only(attr, TRUE);
		str = g_string_new_len(current_value, tmp - current_value);
	} else {
		str = g_string_new(current_value);
	}

	/* empty string */
	if (str->len == 0)
		return TRUE;

	/* split into left and right */
	vals = fu_strsplit(str->str, str->len, ";", 2);

	/* use left half for current value */
	fwupd_bios_setting_set_current_value(attr, vals[0]);
	if (vals[1] == NULL)
		return TRUE;

	/* use the right half to process further */
	right_str = g_string_new(vals[1]);

	/* Strip boot order exclusion info */
	tmp = g_strrstr(right_str->str, LENOVO_EXCLUDED);
	if (tmp != NULL)
		g_string_truncate(str, tmp - right_str->str);

	/* Look for possible values to populate */
	tmp = g_strrstr(right_str->str, LENOVO_POSSIBLE_NEEDLE);
	if (tmp != NULL) {
		g_auto(GStrv) possible_vals = NULL;
		g_string_erase(right_str, 0, strlen(LENOVO_POSSIBLE_NEEDLE));
		possible_vals = fu_strsplit(right_str->str, right_str->len, ",", -1);
		if (possible_vals[0] != NULL)
			fwupd_bios_setting_set_kind(attr, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
		for (guint i = 0; possible_vals[i] != NULL && possible_vals[i][0] != '\0'; i++) {
			/* last string */
			if (possible_vals[i + 1] == NULL &&
			    g_strrstr(possible_vals[i], "]") != NULL) {
				g_auto(GStrv) stripped_vals = fu_strsplit(possible_vals[i],
									  strlen(possible_vals[i]),
									  "]",
									  -1);
				fwupd_bios_setting_add_possible_value(attr, stripped_vals[0]);
				continue;
			}
			fwupd_bios_setting_add_possible_value(attr, possible_vals[i]);
		}
	}
	return TRUE;
}

static gboolean
fu_bios_settings_run_folder_fixups(FwupdBiosSetting *attr, GError **error)
{
	if (fwupd_bios_setting_get_kind(attr) == FWUPD_BIOS_SETTING_KIND_UNKNOWN)
		return fu_bios_setting_fixup_lenovo_thinklmi_bug(attr, error);
	return TRUE;
}

static gboolean
fu_bios_setting_set_type(FuBiosSettings *self, FwupdBiosSetting *attr, GError **error)
{
	gboolean kernel_bug = FALSE;
	g_autofree gchar *data = NULL;
	g_autoptr(GError) error_key = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_BIOS_SETTINGS(self), FALSE);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(attr), FALSE);

	/* lenovo thinklmi seems to be missing it even though it's mandatory :/ */
	if (!fu_bios_setting_get_key(attr, "type", &data, &error_key)) {
#if GLIB_CHECK_VERSION(2, 64, 0)
		g_warning_once("KERNEL BUG: 'type' attribute not exported: (%s)",
			       error_key->message);
#else
		g_debug("KERNEL BUG: 'type' attribute not exported: (%s)", error_key->message);
#endif
		kernel_bug = TRUE;
	}

	if (g_strcmp0(data, "enumeration") == 0 || kernel_bug) {
		if (!fu_bios_setting_set_enumeration_attrs(attr, &error_local)) {
			if (g_getenv("FWUPD_BIOS_SETTING_VERBOSE") != NULL)
				g_debug("failed to add enumeration attrs: %s",
					error_local->message);
		}
	} else if (g_strcmp0(data, "integer") == 0) {
		if (!fu_bios_setting_set_integer_attrs(attr, &error_local)) {
			if (g_getenv("FWUPD_BIOS_SETTING_VERBOSE") != NULL)
				g_debug("failed to add integer attrs: %s", error_local->message);
		}
	} else if (g_strcmp0(data, "string") == 0) {
		if (!fu_bios_setting_set_string_attrs(attr, &error_local)) {
			if (g_getenv("FWUPD_BIOS_SETTING_VERBOSE") != NULL)
				g_debug("failed to add string attrs: %s", error_local->message);
		}
	}
	return TRUE;
}

/* Special case attribute that is a file not a folder
 * https://github.com/torvalds/linux/blob/v5.18/Documentation/ABI/testing/sysfs-class-firmware-attributes#L300
 */
static gboolean
fu_bios_setting_set_file_attributes(FuBiosSettings *self, FwupdBiosSetting *attr, GError **error)
{
	g_autofree gchar *value = NULL;

	if (g_strcmp0(fwupd_bios_setting_get_name(attr), FWUPD_BIOS_SETTING_PENDING_REBOOT) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s attribute is not supported",
			    fwupd_bios_setting_get_name(attr));
		return FALSE;
	}
	if (!fu_bios_setting_set_description(self, attr, error))
		return FALSE;
	if (!fu_bios_setting_get_key(attr, NULL, &value, error))
		return FALSE;
	fwupd_bios_setting_set_current_value(attr, value);
	fwupd_bios_setting_set_read_only(attr, TRUE);

	return TRUE;
}

static gboolean
fu_bios_settings_set_folder_attributes(FuBiosSettings *self, FwupdBiosSetting *attr, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	if (!fu_bios_setting_set_type(self, attr, error))
		return FALSE;
	if (!fu_bios_setting_set_current_value(attr, error))
		return FALSE;
	if (!fu_bios_setting_set_description(self, attr, &error_local))
		g_debug("%s", error_local->message);
	if (!fu_bios_settings_run_folder_fixups(attr, error))
		return FALSE;
	fu_bios_setting_set_read_only(self, attr);
	return TRUE;
}

static gboolean
fu_bios_settings_populate_attribute(FuBiosSettings *self,
				    const gchar *driver,
				    const gchar *path,
				    const gchar *name,
				    GError **error)
{
	g_autoptr(FwupdBiosSetting) attr = NULL;
	g_autofree gchar *id = NULL;

	g_return_val_if_fail(FU_IS_BIOS_SETTINGS(self), FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(driver != NULL, FALSE);

	attr = fwupd_bios_setting_new(name, path);

	id = g_strdup_printf("com.%s.%s", driver, name);
	fwupd_bios_setting_set_id(attr, id);

	if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
		if (!fu_bios_settings_set_folder_attributes(self, attr, error))
			return FALSE;
	} else {
		if (!fu_bios_setting_set_file_attributes(self, attr, error))
			return FALSE;
	}

	g_ptr_array_add(self->attrs, g_object_ref(attr));

	return TRUE;
}

static void
fu_bios_settings_populate_descriptions(FuBiosSettings *self)
{
	g_return_if_fail(FU_IS_BIOS_SETTINGS(self));

	g_hash_table_insert(self->descriptions,
			    g_strdup("pending_reboot"),
			    /* TRANSLATORS: description of a BIOS setting */
			    g_strdup(_("Settings will apply after system reboots")));
	g_hash_table_insert(self->descriptions,
			    g_strdup("com.thinklmi.WindowsUEFIFirmwareUpdate"),
			    /* TRANSLATORS: description of a BIOS setting */
			    g_strdup(_("BIOS updates delivered via LVFS or Windows Update")));
}

static void
fu_bios_settings_populate_read_only(FuBiosSettings *self)
{
	g_return_if_fail(FU_IS_BIOS_SETTINGS(self));

	g_hash_table_insert(self->read_only,
			    g_strdup("com.thinklmi.SecureBoot"),
			    g_strdup(_("Enable")));
	g_hash_table_insert(self->read_only,
			    g_strdup("com.dell-wmi-sysman.SecureBoot"),
			    g_strdup(_("Enabled")));
}

static void
fu_bios_settings_combination_fixups(FuBiosSettings *self)
{
	FwupdBiosSetting *thinklmi_sb = fu_bios_settings_get_attr(self, "com.thinklmi.SecureBoot");
	FwupdBiosSetting *thinklmi_3rd =
	    fu_bios_settings_get_attr(self, "com.thinklmi.Allow3rdPartyUEFICA");

	if (thinklmi_sb != NULL && thinklmi_3rd != NULL) {
		const gchar *val = fwupd_bios_setting_get_current_value(thinklmi_3rd);
		if (g_strcmp0(val, "Disable") == 0) {
			g_debug("Disabling changing %s since %s is %s",
				fwupd_bios_setting_get_name(thinklmi_sb),
				fwupd_bios_setting_get_name(thinklmi_3rd),
				val);
			fwupd_bios_setting_set_read_only(thinklmi_sb, TRUE);
		}
	}
}

/**
 * fu_bios_settings_setup:
 * @self: a #FuBiosSettings
 *
 * Clears all attributes and re-initializes them.
 * Mostly used for the test suite, but could potentially be connected to udev
 * events for drivers being loaded or unloaded too.
 *
 * Since: 1.8.4
 **/
gboolean
fu_bios_settings_setup(FuBiosSettings *self, GError **error)
{
	guint count = 0;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(GDir) class_dir = NULL;

	g_return_val_if_fail(FU_IS_BIOS_SETTINGS(self), FALSE);

	if (self->attrs->len > 0) {
		g_debug("re-initializing attributes");
		g_ptr_array_set_size(self->attrs, 0);
	}
	if (g_hash_table_size(self->descriptions) == 0)
		fu_bios_settings_populate_descriptions(self);

	if (g_hash_table_size(self->read_only) == 0)
		fu_bios_settings_populate_read_only(self);

	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW_ATTRIB);
	class_dir = g_dir_open(sysfsfwdir, 0, error);
	if (class_dir == NULL)
		return FALSE;

	do {
		g_autofree gchar *path = NULL;
		g_autoptr(GDir) driver_dir = NULL;
		const gchar *driver = g_dir_read_name(class_dir);
		if (driver == NULL)
			break;
		path = g_build_filename(sysfsfwdir, driver, "attributes", NULL);
		if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
			g_debug("skipping non-directory %s", path);
			continue;
		}
		driver_dir = g_dir_open(path, 0, error);
		if (driver_dir == NULL)
			return FALSE;
		do {
			const gchar *name = g_dir_read_name(driver_dir);
			g_autofree gchar *full_path = NULL;
			g_autoptr(GError) error_local = NULL;
			if (name == NULL)
				break;
			full_path = g_build_filename(path, name, NULL);
			if (!fu_bios_settings_populate_attribute(self,
								 driver,
								 full_path,
								 name,
								 &error_local)) {
				if (g_error_matches(error_local,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED)) {
					g_debug("%s is not supported", name);
					continue;
				}
				g_propagate_error(error, g_steal_pointer(&error_local));
				return FALSE;
			}
		} while (++count);
	} while (TRUE);
	g_debug("loaded %u BIOS settings", count);

	fu_bios_settings_combination_fixups(self);

	return TRUE;
}

static void
fu_bios_settings_init(FuBiosSettings *self)
{
	self->attrs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->descriptions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	self->read_only = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

/**
 * fu_bios_settings_get_attr:
 * @self: a #FuBiosSettings
 * @val: the attribute ID or name to check for
 *
 * Returns: (transfer none): the attribute with the given ID or name or NULL if it doesn't exist.
 *
 * Since: 1.8.4
 **/
FwupdBiosSetting *
fu_bios_settings_get_attr(FuBiosSettings *self, const gchar *val)
{
	g_return_val_if_fail(FU_IS_BIOS_SETTINGS(self), NULL);
	g_return_val_if_fail(val != NULL, NULL);

	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdBiosSetting *attr = g_ptr_array_index(self->attrs, i);
		const gchar *tmp_id = fwupd_bios_setting_get_id(attr);
		const gchar *tmp_name = fwupd_bios_setting_get_name(attr);
		if (g_strcmp0(val, tmp_id) == 0 || g_strcmp0(val, tmp_name) == 0)
			return attr;
	}
	return NULL;
}

/**
 * fu_bios_settings_get_all:
 * @self: a #FuBiosSettings
 *
 * Gets all the attributes in the object.
 *
 * Returns: (transfer container) (element-type FwupdBiosSetting): attributes
 *
 * Since: 1.8.4
 **/
GPtrArray *
fu_bios_settings_get_all(FuBiosSettings *self)
{
	g_return_val_if_fail(FU_IS_BIOS_SETTINGS(self), NULL);
	return g_ptr_array_ref(self->attrs);
}

/**
 * fu_bios_settings_get_pending_reboot:
 * @self: a #FuBiosSettings
 * @result: (out): Whether a reboot is pending
 * @error: (nullable): optional return location for an error
 *
 * Determines if the system will apply changes to attributes upon reboot
 *
 * Since: 1.8.4
 **/
gboolean
fu_bios_settings_get_pending_reboot(FuBiosSettings *self, gboolean *result, GError **error)
{
	FwupdBiosSetting *attr;
	g_autofree gchar *data = NULL;
	guint64 val = 0;

	g_return_val_if_fail(result != NULL, FALSE);
	g_return_val_if_fail(FU_IS_BIOS_SETTINGS(self), FALSE);

	for (guint i = 0; i < self->attrs->len; i++) {
		const gchar *tmp;

		attr = g_ptr_array_index(self->attrs, i);
		tmp = fwupd_bios_setting_get_name(attr);
		if (g_strcmp0(tmp, FWUPD_BIOS_SETTING_PENDING_REBOOT) == 0)
			break;
		attr = NULL;
	}

	if (attr == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "failed to find pending reboot attribute");
		return FALSE;
	}

	/* refresh/re-read */
	if (!fu_bios_setting_get_key(attr, NULL, &data, error))
		return FALSE;
	fwupd_bios_setting_set_current_value(attr, data);
	if (!fu_strtoull(data, &val, 0, G_MAXUINT32, error))
		return FALSE;

	*result = (val == 1);

	return TRUE;
}

/**
 * fu_bios_settings_to_variant:
 * @self: a #FuBiosSettings
 * @trusted: whether the caller should receive trusted values
 *
 * Serializes the #FwupdBiosSetting objects.
 *
 * Returns: a #GVariant or %NULL
 *
 * Since: 1.8.4
 **/
GVariant *
fu_bios_settings_to_variant(FuBiosSettings *self, gboolean trusted)
{
	GVariantBuilder builder;

	g_return_val_if_fail(FU_IS_BIOS_SETTINGS(self), NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdBiosSetting *bios_setting = g_ptr_array_index(self->attrs, i);
		g_variant_builder_add_value(&builder,
					    fwupd_bios_setting_to_variant(bios_setting, trusted));
	}
	return g_variant_new("(aa{sv})", &builder);
}

/**
 * fu_bios_settings_from_json:
 * @self: a #FuBiosSettings
 * @json_node: a Json node to parse from
 * @error: (nullable): optional return location for an error
 *
 * Loads #FwupdBiosSetting objects from a JSON node.
 *
 * Returns: TRUE if the objects were imported
 *
 * Since: 1.8.4
 **/
gboolean
fu_bios_settings_from_json(FuBiosSettings *self, JsonNode *json_node, GError **error)
{
	JsonArray *array;
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}
	obj = json_node_get_object(json_node);

	/* this has to exist */
	if (!json_object_has_member(obj, "BiosSettings")) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "no BiosSettings property in object");
		return FALSE;
	}
	array = json_object_get_array_member(obj, "BiosSettings");
	for (guint i = 0; i < json_array_get_length(array); i++) {
		JsonNode *node_tmp = json_array_get_element(array, i);
		g_autoptr(FwupdBiosSetting) attr = fwupd_bios_setting_new(NULL, NULL);
		if (!fwupd_bios_setting_from_json(attr, node_tmp, error))
			return FALSE;
		g_ptr_array_add(self->attrs, g_steal_pointer(&attr));
	}

	/* success */
	return TRUE;
}

/**
 * fu_bios_settings_from_json_file:
 * @self: A #FuBiosSettings
 * @fn: a path to a JSON file
 * @error: (nullable): optional return location for an error
 *
 * Adds all BIOS attributes from a JSON filename
 *
 * Returns: TRUE for success, FALSE for failure
 *
 * Since: 1.8.4
 **/
gboolean
fu_bios_settings_from_json_file(FuBiosSettings *self, const gchar *fn, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();

	if (!g_file_get_contents(fn, &data, NULL, error))
		return FALSE;
	if (!json_parser_load_from_data(parser, data, -1, error)) {
		g_prefix_error(error, "%s doesn't look like JSON data: ", fn);
		return FALSE;
	}
	return fu_bios_settings_from_json(self, json_parser_get_root(parser), error);
}

/**
 * fu_bios_settings_to_hash_kv:
 * @self: A #FuBiosSettings
 *
 * Creates a #GHashTable with the name and current value of
 * all BIOS settings.
 *
 * Returns: (transfer full): name/value pairs
 * Since: 1.8.4
 **/
GHashTable *
fu_bios_settings_to_hash_kv(FuBiosSettings *self)
{
	GHashTable *bios_settings = NULL;

	g_return_val_if_fail(self != NULL, NULL);

	bios_settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdBiosSetting *item_setting = g_ptr_array_index(self->attrs, i);
		g_hash_table_insert(bios_settings,
				    g_strdup(fwupd_bios_setting_get_id(item_setting)),
				    g_strdup(fwupd_bios_setting_get_current_value(item_setting)));
	}
	return bios_settings;
}

/**
 * fu_bios_settings_new:
 *
 * Returns: #FuBiosSettings
 *
 * Since: 1.8.4
 **/
FuBiosSettings *
fu_bios_settings_new(void)
{
	return g_object_new(FU_TYPE_FIRMWARE_ATTRS, NULL);
}
