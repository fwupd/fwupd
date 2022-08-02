/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBiosAttrs"

#include "config.h"

#include "fwupd-bios-attr-private.h"
#include "fwupd-error.h"

#include "fu-bios-attrs-private.h"
#include "fu-path.h"
#include "fu-string.h"

struct _FuBiosAttrs {
	GObject parent_instance;
	gboolean kernel_bug_shown;
	GPtrArray *attrs;
};

G_DEFINE_TYPE(FuBiosAttrs, fu_bios_attrs, G_TYPE_OBJECT)

static void
fu_bios_attrs_finalize(GObject *obj)
{
	FuBiosAttrs *self = FU_BIOS_ATTRS(obj);
	g_ptr_array_unref(self->attrs);
	G_OBJECT_CLASS(fu_bios_attrs_parent_class)->finalize(obj);
}

static void
fu_bios_attrs_class_init(FuBiosAttrsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_bios_attrs_finalize;
}

static gboolean
fu_bios_attr_get_key(FwupdBiosAttr *attr, const gchar *key, gchar **value_out, GError **error)
{
	g_autofree gchar *tmp = NULL;

	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(attr), FALSE);
	g_return_val_if_fail(&value_out != NULL, FALSE);

	tmp = g_build_filename(fwupd_bios_attr_get_path(attr), key, NULL);
	if (!g_file_get_contents(tmp, value_out, NULL, error)) {
		g_prefix_error(error, "failed to load %s: ", key);
		return FALSE;
	}
	g_strchomp(*value_out);
	return TRUE;
}

static gboolean
fu_bios_attr_set_description(FwupdBiosAttr *attr, GError **error)
{
	g_autofree gchar *data = NULL;

	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(attr), FALSE);

	if (!fu_bios_attr_get_key(attr, "display_name", &data, error))
		return FALSE;
	fwupd_bios_attr_set_description(attr, data);

	return TRUE;
}

static guint64
fu_bios_attr_get_key_as_integer(FwupdBiosAttr *attr, const gchar *key, GError **error)
{
	g_autofree gchar *str = NULL;
	guint64 tmp;

	if (!fu_bios_attr_get_key(attr, key, &str, error))
		return G_MAXUINT64;
	if (!fu_strtoull(str, &tmp, 0, G_MAXUINT64, error)) {
		g_prefix_error(error, "failed to convert %s to integer: ", key);
		return G_MAXUINT64;
	}
	return tmp;
}

static gboolean
fu_bios_attr_set_enumeration_attrs(FwupdBiosAttr *attr, GError **error)
{
	const gchar *delimiters[] = {",", ";", NULL};
	g_autofree gchar *str = NULL;

	if (!fu_bios_attr_get_key(attr, "possible_values", &str, error))
		return FALSE;
	for (guint j = 0; delimiters[j] != NULL; j++) {
		g_auto(GStrv) vals = NULL;
		if (g_strrstr(str, delimiters[j]) == NULL)
			continue;
		vals = fu_strsplit(str, strlen(str), delimiters[j], -1);
		for (guint i = 0; vals[i] != NULL && vals[i][0] != '\0'; i++)
			fwupd_bios_attr_add_possible_value(attr, vals[i]);
	}
	return TRUE;
}

static gboolean
fu_bios_attr_set_string_attrs(FwupdBiosAttr *attr, GError **error)
{
	guint64 tmp;

	tmp = fu_bios_attr_get_key_as_integer(attr, "min_length", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_attr_set_lower_bound(attr, tmp);
	tmp = fu_bios_attr_get_key_as_integer(attr, "max_length", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_attr_set_upper_bound(attr, tmp);
	return TRUE;
}

static gboolean
fu_bios_attr_set_integer_attrs(FwupdBiosAttr *attr, GError **error)
{
	guint64 tmp;

	tmp = fu_bios_attr_get_key_as_integer(attr, "min_value", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_attr_set_lower_bound(attr, tmp);
	tmp = fu_bios_attr_get_key_as_integer(attr, "max_value", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_attr_set_upper_bound(attr, tmp);
	tmp = fu_bios_attr_get_key_as_integer(attr, "scalar_increment", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fwupd_bios_attr_set_scalar_increment(attr, tmp);

	return TRUE;
}

static gboolean
fu_bios_attr_set_current_value(FwupdBiosAttr *attr, GError **error)
{
	g_autofree gchar *str = NULL;

	if (!fu_bios_attr_get_key(attr, "current_value", &str, error))
		return FALSE;
	fwupd_bios_attr_set_current_value(attr, str);
	return TRUE;
}

#define LENOVO_POSSIBLE_NEEDLE	"[Optional:"
#define LENOVO_READ_ONLY_NEEDLE "[Status:ShowOnly]"
#define LENOVO_EXCLUDED		"[Excluded from boot order:"

static gboolean
fu_bios_attr_fixup_lenovo_thinklmi_bug(FwupdBiosAttr *attr, GError **error)
{
	const gchar *current_value = fwupd_bios_attr_get_current_value(attr);
	const gchar *tmp;
	g_autoptr(GString) str = NULL;
	g_autoptr(GString) right_str = NULL;
	g_auto(GStrv) vals = NULL;

	g_debug("Processing %s", fwupd_bios_attr_get_current_value(attr));

	/* We have read only */
	tmp = g_strrstr(current_value, LENOVO_READ_ONLY_NEEDLE);
	if (tmp != NULL) {
		fwupd_bios_attr_set_read_only(attr, TRUE);
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
	fwupd_bios_attr_set_current_value(attr, vals[0]);
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
		for (guint i = 0; possible_vals[i] != NULL && possible_vals[i][0] != '\0'; i++) {
			/* last string */
			if (possible_vals[i + 1] == NULL &&
			    g_strrstr(possible_vals[i], "]") != NULL) {
				g_auto(GStrv) stripped_vals = fu_strsplit(possible_vals[i],
									  strlen(possible_vals[i]),
									  "]",
									  -1);
				fwupd_bios_attr_add_possible_value(attr, stripped_vals[0]);
				continue;
			}
			fwupd_bios_attr_add_possible_value(attr, possible_vals[i]);
		}
	}
	return TRUE;
}

static gboolean
fu_bios_attr_set_type(FuBiosAttrs *self, FwupdBiosAttr *attr, const gchar *driver, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(GError) error_key = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_BIOS_ATTRS(self), FALSE);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(attr), FALSE);
	g_return_val_if_fail(driver != NULL, FALSE);

	/* lenovo thinklmi seems to be missing it even though it's mandatory :/ */
	if (!fu_bios_attr_get_key(attr, "type", &data, &error_key)) {
		g_debug("%s", error_key->message);
		if (!self->kernel_bug_shown) {
			g_warning("KERNEL BUG: %s doesn't export a 'type' attribute", driver);
			self->kernel_bug_shown = TRUE;
		}
		data = g_strdup("enumeration");
	}

	if (g_strcmp0(data, "enumeration") == 0) {
		fwupd_bios_attr_set_kind(attr, FWUPD_BIOS_ATTR_KIND_ENUMERATION);
		if (!fu_bios_attr_set_enumeration_attrs(attr, &error_local))
			g_debug("failed to add enumeration attrs: %s", error_local->message);
	} else if (g_strcmp0(data, "integer") == 0) {
		fwupd_bios_attr_set_kind(attr, FWUPD_BIOS_ATTR_KIND_INTEGER);
		if (!fu_bios_attr_set_integer_attrs(attr, &error_local))
			g_debug("failed to add integer attrs: %s", error_local->message);
	} else if (g_strcmp0(data, "string") == 0) {
		fwupd_bios_attr_set_kind(attr, FWUPD_BIOS_ATTR_KIND_STRING);
		if (!fu_bios_attr_set_string_attrs(attr, &error_local))
			g_debug("failed to add string attrs: %s", error_local->message);
	}
	return TRUE;
}

/* Special case attribute that is a file not a folder
 * https://github.com/torvalds/linux/blob/v5.18/Documentation/ABI/testing/sysfs-class-firmware-attributes#L300
 */
static gboolean
fu_bios_attr_set_file_attributes(FwupdBiosAttr *attr, GError **error)
{
	g_autofree gchar *value = NULL;

	if (g_strcmp0(fwupd_bios_attr_get_name(attr), FWUPD_BIOS_ATTR_PENDING_REBOOT) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s attribute is not supported",
			    fwupd_bios_attr_get_name(attr));
		return FALSE;
	}
	if (!fu_bios_attr_get_key(attr, NULL, &value, error))
		return FALSE;
	fwupd_bios_attr_set_current_value(attr, value);
	fwupd_bios_attr_set_read_only(attr, TRUE);

	return TRUE;
}

static gboolean
fu_bios_attrs_set_folder_attributes(FuBiosAttrs *self,
				    FwupdBiosAttr *attr,
				    const gchar *driver,
				    GError **error)
{
	g_autoptr(GError) error_local = NULL;

	if (!fu_bios_attr_set_type(self, attr, driver, error))
		return FALSE;
	if (!fu_bios_attr_set_current_value(attr, error))
		return FALSE;
	if (!fu_bios_attr_set_description(attr, &error_local))
		g_debug("%s", error_local->message);
	if (self->kernel_bug_shown) {
		if (!fu_bios_attr_fixup_lenovo_thinklmi_bug(attr, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_bios_attrs_populate_attribute(FuBiosAttrs *self,
				 const gchar *driver,
				 const gchar *path,
				 const gchar *name,
				 GError **error)
{
	g_autoptr(FwupdBiosAttr) attr = NULL;
	g_autofree gchar *id = NULL;

	g_return_val_if_fail(FU_IS_BIOS_ATTRS(self), FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(driver != NULL, FALSE);

	attr = fwupd_bios_attr_new(name, path);

	if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
		if (!fu_bios_attrs_set_folder_attributes(self, attr, driver, error))
			return FALSE;
	} else {
		if (!fu_bios_attr_set_file_attributes(attr, error))
			return FALSE;
	}

	id = g_strdup_printf("com.%s.%s", driver, name);
	fwupd_bios_attr_set_id(attr, id);

	g_ptr_array_add(self->attrs, g_object_ref(attr));

	return TRUE;
}

/**
 * fu_bios_attrs_setup:
 * @self: a #FuBiosAttrs
 *
 * Clears all attributes and re-initializes them.
 * Mostly used for the test suite, but could potentially be connected to udev
 * events for drivers being loaded or unloaded too.
 *
 * Since: 1.8.4
 **/
gboolean
fu_bios_attrs_setup(FuBiosAttrs *self, GError **error)
{
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(GDir) class_dir = NULL;

	g_return_val_if_fail(FU_IS_BIOS_ATTRS(self), FALSE);

	if (self->attrs->len > 0) {
		g_debug("re-initializing attributes");
		g_ptr_array_set_size(self->attrs, 0);
	}

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
			if (!fu_bios_attrs_populate_attribute(self,
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
		} while (TRUE);
	} while (TRUE);

	return TRUE;
}

static void
fu_bios_attrs_init(FuBiosAttrs *self)
{
	self->attrs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

/**
 * fu_bios_attrs_get_attr:
 * @self: a #FuBiosAttrs
 * @val: the attribute ID or name to check for
 *
 * Returns: (transfer none): the attribute with the given ID or name or NULL if it doesn't exist.
 *
 * Since: 1.8.4
 **/
FwupdBiosAttr *
fu_bios_attrs_get_attr(FuBiosAttrs *self, const gchar *val)
{
	g_return_val_if_fail(FU_IS_BIOS_ATTRS(self), NULL);
	g_return_val_if_fail(val != NULL, NULL);

	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdBiosAttr *attr = g_ptr_array_index(self->attrs, i);
		const gchar *tmp_id = fwupd_bios_attr_get_id(attr);
		const gchar *tmp_name = fwupd_bios_attr_get_name(attr);
		if (g_strcmp0(val, tmp_id) == 0 || g_strcmp0(val, tmp_name) == 0)
			return attr;
	}
	return NULL;
}

/**
 * fu_bios_attrs_get_all:
 * @self: a #FuBiosAttrs
 *
 * Gets all the attributes in the object.
 *
 * Returns: (transfer container) (element-type FwupdBiosAttr): attributes
 *
 * Since: 1.8.4
 **/
GPtrArray *
fu_bios_attrs_get_all(FuBiosAttrs *self)
{
	g_return_val_if_fail(FU_IS_BIOS_ATTRS(self), NULL);
	return g_ptr_array_ref(self->attrs);
}

/**
 * fu_bios_attrs_get_pending_reboot:
 * @self: a #FuBiosAttrs
 * @result: (out): Whether a reboot is pending
 * @error: (nullable): optional return location for an error
 *
 * Determines if the system will apply changes to attributes upon reboot
 *
 * Since: 1.8.4
 **/
gboolean
fu_bios_attrs_get_pending_reboot(FuBiosAttrs *self, gboolean *result, GError **error)
{
	FwupdBiosAttr *attr;
	const gchar *data;
	guint64 val = 0;

	g_return_val_if_fail(result != NULL, FALSE);
	g_return_val_if_fail(FU_IS_BIOS_ATTRS(self), FALSE);

	for (guint i = 0; i < self->attrs->len; i++) {
		const gchar *tmp;

		attr = g_ptr_array_index(self->attrs, i);
		tmp = fwupd_bios_attr_get_name(attr);
		if (g_strcmp0(tmp, FWUPD_BIOS_ATTR_PENDING_REBOOT) == 0)
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

	data = fwupd_bios_attr_get_current_value(attr);
	if (!fu_strtoull(data, &val, 0, G_MAXUINT32, error))
		return FALSE;

	*result = (val == 1);

	return TRUE;
}

/**
 * fu_bios_attrs_to_variant:
 * @self: a #FuBiosAttrs
 *
 * Serializes the #FwupdBiosAttr objects.
 *
 * Returns: a #GVariant or %NULL
 *
 * Since: 1.8.4
 **/
GVariant *
fu_bios_attrs_to_variant(FuBiosAttrs *self)
{
	GVariantBuilder builder;

	g_return_val_if_fail(FU_IS_BIOS_ATTRS(self), NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdBiosAttr *bios_attr = g_ptr_array_index(self->attrs, i);
		GVariant *tmp = fwupd_bios_attr_to_variant(bios_attr);
		g_variant_builder_add_value(&builder, tmp);
	}
	return g_variant_new("(aa{sv})", &builder);
}

/**
 * fu_bios_attrs_from_json:
 * @self: a #FuBiosAttrs
 *
 * Loads #FwupdBiosAttr objects from a JSON node.
 *
 * Returns: TRUE if the objects were imported
 *
 * Since: 1.8.4
 **/
gboolean
fu_bios_attrs_from_json(FuBiosAttrs *self, JsonNode *json_node, GError **error)
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
	if (!json_object_has_member(obj, "BiosAttributes")) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "no BiosAttributes property in object");
		return FALSE;
	}
	array = json_object_get_array_member(obj, "BiosAttributes");
	for (guint i = 0; i < json_array_get_length(array); i++) {
		JsonNode *node_tmp = json_array_get_element(array, i);
		g_autoptr(FwupdBiosAttr) attr = fwupd_bios_attr_new(NULL, NULL);
		if (!fwupd_bios_attr_from_json(attr, node_tmp, error))
			return FALSE;
		g_ptr_array_add(self->attrs, g_steal_pointer(&attr));
	}

	/* success */
	return TRUE;
}

/**
 * fu_bios_attrs_new:
 *
 * Returns: #FuBiosAttrs
 *
 * Since: 1.8.4
 **/
FuBiosAttrs *
fu_bios_attrs_new(void)
{
	return g_object_new(FU_TYPE_FIRMWARE_ATTRS, NULL);
}
