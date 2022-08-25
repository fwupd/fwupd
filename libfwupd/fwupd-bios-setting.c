/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-bios-setting-private.h"
#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"

/**
 * FwupdBiosSetting:
 *
 * A BIOS setting that represents a setting in the firmware.
 */

static void
fwupd_bios_setting_finalize(GObject *object);

typedef struct {
	FwupdBiosSettingKind kind;
	gchar *id;
	gchar *name;
	gchar *description;
	gchar *path;
	gchar *current_value;
	guint64 lower_bound;
	guint64 upper_bound;
	guint64 scalar_increment;
	gboolean read_only;
	GPtrArray *possible_values;
} FwupdBiosSettingPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FwupdBiosSetting, fwupd_bios_setting, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_bios_setting_get_instance_private(o))

/**
 * fwupd_bios_setting_get_id
 * @self: a #FwupdBiosSetting
 *
 * Gets the unique attribute identifier for this attribute/driver
 *
 * Returns: attribute ID if set otherwise NULL
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_setting_get_id(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);
	return priv->id;
}

/**
 * fwupd_bios_setting_set_id
 * @self: a #FwupdBiosSetting
 *
 * Sets the unique attribute identifier for this attribute
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_id(FwupdBiosSetting *self, const gchar *id)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));

	/* not changed */
	if (g_strcmp0(priv->id, id) == 0)
		return;

	g_free(priv->id);
	priv->id = g_strdup(id);
}

/**
 * fwupd_bios_setting_get_read_only:
 * @self: a #FwupdBiosSetting
 *
 * Determines if a BIOS setting is read only
 *
 * Returns: gboolean
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_bios_setting_get_read_only(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), FALSE);
	return priv->read_only;
}

/**
 * fwupd_bios_setting_set_read_only:
 * @self: a #FwupdBiosSetting
 *
 * Configures whether an attribute is read only
 * maximum length for string attributes.
 *
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_read_only(FwupdBiosSetting *self, gboolean val)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));
	priv->read_only = val;
}

/**
 * fwupd_bios_setting_get_lower_bound:
 * @self: a #FwupdBiosSetting
 *
 * Gets the lower bound for integer attributes or
 * minimum length for string attributes.
 *
 * Returns: guint64
 *
 * Since: 1.8.4
 **/
guint64
fwupd_bios_setting_get_lower_bound(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), 0);
	return priv->lower_bound;
}

/**
 * fwupd_bios_setting_get_upper_bound:
 * @self: a #FwupdBiosSetting
 *
 * Gets the upper bound for integer attributes or
 * maximum length for string attributes.
 *
 * Returns: guint64
 *
 * Since: 1.8.4
 **/
guint64
fwupd_bios_setting_get_upper_bound(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), 0);
	return priv->upper_bound;
}

/**
 * fwupd_bios_setting_get_scalar_increment:
 * @self: a #FwupdBiosSetting
 *
 * Gets the scalar increment used for integer attributes.
 *
 * Returns: guint64
 *
 * Since: 1.8.4
 **/
guint64
fwupd_bios_setting_get_scalar_increment(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), 0);
	return priv->scalar_increment;
}

/**
 * fwupd_bios_setting_set_upper_bound:
 * @self: a #FwupdBiosSetting
 * @val: a guint64 value to set bound to
 *
 * Sets the upper bound used for BIOS integer attributes or max
 * length for string attributes.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_upper_bound(FwupdBiosSetting *self, guint64 val)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));
	priv->upper_bound = val;
}

/**
 * fwupd_bios_setting_set_lower_bound:
 * @self: a #FwupdBiosSetting
 * @val: a guint64 value to set bound to
 *
 * Sets the lower bound used for BIOS integer attributes or max
 * length for string attributes.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_lower_bound(FwupdBiosSetting *self, guint64 val)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));
	priv->lower_bound = val;
}

/**
 * fwupd_bios_setting_set_scalar_increment:
 * @self: a #FwupdBiosSetting
 * @val: a guint64 value to set increment to
 *
 * Sets the scalar increment used for BIOS integer attributes.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_scalar_increment(FwupdBiosSetting *self, guint64 val)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));
	priv->scalar_increment = val;
}

/**
 * fwupd_bios_setting_get_kind:
 * @self: a #FwupdBiosSetting
 *
 * Gets the BIOS setting type used by the kernel interface.
 *
 * Returns: the bios setting type, or %FWUPD_BIOS_SETTING_KIND_UNKNOWN if unset.
 *
 * Since: 1.8.4
 **/
FwupdBiosSettingKind
fwupd_bios_setting_get_kind(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), 0);
	return priv->kind;
}

/**
 * fwupd_bios_setting_set_kind:
 * @self: a #FwupdBiosSetting
 * @type: a bios setting type, e.g. %FWUPD_BIOS_SETTING_KIND_ENUMERATION
 *
 * Sets the BIOS setting type used by the kernel interface.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_kind(FwupdBiosSetting *self, FwupdBiosSettingKind type)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));
	priv->kind = type;
}

/**
 * fwupd_bios_setting_set_name:
 * @self: a #FwupdBiosSetting
 * @name: (nullable): the attribute name
 *
 * Sets the attribute name provided by a kernel driver.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_name(FwupdBiosSetting *self, const gchar *name)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));

	/* not changed */
	if (g_strcmp0(priv->name, name) == 0)
		return;

	g_free(priv->name);
	priv->name = g_strdup(name);
}

/**
 * fwupd_bios_setting_set_path:
 * @self: a #FwupdBiosSetting
 * @path: (nullable): the path the driver providing the attribute uses
 *
 * Sets path to the attribute.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_path(FwupdBiosSetting *self, const gchar *path)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));

	/* not changed */
	if (g_strcmp0(priv->path, path) == 0)
		return;

	g_free(priv->path);
	priv->path = g_strdup(path);
}

/**
 * fwupd_bios_setting_set_description:
 * @self: a #FwupdBiosSetting
 * @description: (nullable): the attribute description
 *
 * Sets the attribute description.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_description(FwupdBiosSetting *self, const gchar *description)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));

	/* not changed */
	if (g_strcmp0(priv->description, description) == 0)
		return;

	g_free(priv->description);
	priv->description = g_strdup(description);
}

/* determine if key is supposed to be positive */
static gboolean
fu_bios_setting_key_is_positive(const gchar *key)
{
	if (g_strrstr(key, "enable"))
		return TRUE;
	if (g_strcmp0(key, "true") == 0)
		return TRUE;
	if (g_strcmp0(key, "1") == 0)
		return TRUE;
	if (g_strcmp0(key, "on") == 0)
		return TRUE;
	return FALSE;
}

/* determine if key is supposed to be negative */
static gboolean
fu_bios_setting_key_is_negative(const gchar *key)
{
	if (g_strrstr(key, "disable"))
		return TRUE;
	if (g_strcmp0(key, "false") == 0)
		return TRUE;
	if (g_strcmp0(key, "0") == 0)
		return TRUE;
	if (g_strcmp0(key, "off") == 0)
		return TRUE;
	return FALSE;
}

/**
 * fwupd_bios_setting_map_possible_value:
 * @self: a #FwupdBiosSetting
 * @key: the string to try to map
 * @error: (nullable): optional return location for an error
 *
 * Attempts to map a user provided string into strings that a #FwupdBiosSetting can
 * support.  The following heuristics are used:
 * - Ignore case sensitivity
 * - Map obviously "positive" phrases into a value that turns on the #FwupdBiosSetting
 * - Map obviously "negative" phrases into a value that turns off the #FwupdBiosSetting
 *
 * Returns: (transfer none): the possible value that maps or NULL if none if found
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_setting_map_possible_value(FwupdBiosSetting *self, const gchar *key, GError **error)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	gboolean positive_key = FALSE;
	gboolean negative_key = FALSE;
	g_autofree gchar *lower_key = NULL;

	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);
	g_return_val_if_fail(priv->kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION, NULL);

	if (priv->possible_values->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s doesn't contain any possible values",
			    priv->name);
		return NULL;
	}

	lower_key = g_utf8_strdown(key, -1);
	positive_key = fu_bios_setting_key_is_positive(lower_key);
	negative_key = fu_bios_setting_key_is_negative(lower_key);
	for (guint i = 0; i < priv->possible_values->len; i++) {
		const gchar *possible = g_ptr_array_index(priv->possible_values, i);
		g_autofree gchar *lower_possible = g_utf8_strdown(possible, -1);
		gboolean positive_possible;
		gboolean negative_possible;

		/* perfect match */
		if (g_strcmp0(lower_possible, lower_key) == 0)
			return possible;
		/* fuzzy match */
		positive_possible = fu_bios_setting_key_is_positive(lower_possible);
		negative_possible = fu_bios_setting_key_is_negative(lower_possible);
		if ((positive_possible && positive_key) || (negative_possible && negative_key))
			return possible;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "%s doesn't map to any possible values for %s",
		    key,
		    priv->name);
	return NULL;
}

/**
 * fwupd_bios_setting_has_possible_value:
 * @self: a #FwupdBiosSetting
 * @val: the possible value string
 *
 * Finds out if a specific possible value was added to the attribute.
 *
 * Returns: %TRUE if the self matches.
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_bios_setting_has_possible_value(FwupdBiosSetting *self, const gchar *val)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), FALSE);
	g_return_val_if_fail(val != NULL, FALSE);

	if (priv->possible_values->len == 0)
		return TRUE;

	for (guint i = 0; i < priv->possible_values->len; i++) {
		const gchar *tmp = g_ptr_array_index(priv->possible_values, i);
		if (g_strcmp0(tmp, val) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_bios_setting_add_possible_value:
 * @self: a #FwupdBiosSetting
 * @possible_value: the possible
 *
 * Adds a possible value to the attribute.  This indicates one of the values the
 * kernel driver will accept from userspace.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_add_possible_value(FwupdBiosSetting *self, const gchar *possible_value)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));
	if (priv->possible_values->len > 0 &&
	    fwupd_bios_setting_has_possible_value(self, possible_value))
		return;
	g_ptr_array_add(priv->possible_values, g_strdup(possible_value));
}

/**
 * fwupd_bios_setting_get_possible_values:
 * @self: a #FwupdBiosSetting
 *
 * Find all possible values for an enumeration attribute.
 *
 * Returns: (transfer container) (element-type gchar*): all possible values.
 *
 * Since: 1.8.4
 **/
GPtrArray *
fwupd_bios_setting_get_possible_values(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);
	g_return_val_if_fail(priv->kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION, NULL);
	return priv->possible_values;
}

/**
 * fwupd_bios_setting_get_name:
 * @self: a #FwupdBiosSetting
 *
 * Gets the attribute name.
 *
 * Returns: the attribute name, or %NULL if unset.
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_setting_get_name(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);
	return priv->name;
}

/**
 * fwupd_bios_setting_get_path:
 * @self: a #FwupdBiosSetting
 *
 * Gets the path for the driver providing the attribute.
 *
 * Returns: (nullable): the driver, or %NULL if unfound.
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_setting_get_path(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);
	return priv->path;
}

/**
 * fwupd_bios_setting_get_description:
 * @self: a #FwupdBiosSetting
 *
 * Gets the attribute description which is provided by some drivers to explain
 * what they change.
 *
 * Returns: the attribute description, or %NULL if unset.
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_setting_get_description(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);
	return priv->description;
}

/**
 * fwupd_bios_setting_get_current_value:
 * @self: a #FwupdBiosSetting
 *
 * Gets the string representation of the current_value stored in an attribute
 * from the kernel.  This value is cached; so changing it outside of fwupd may
 * may put it out of sync.
 *
 * Returns: the current value of the attribute.
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_setting_get_current_value(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);
	return priv->current_value;
}

/**
 * fwupd_bios_setting_set_current_value:
 * @self: a #FwupdBiosSetting
 * @value: The string to set an attribute to
 *
 * Sets the string stored in an attribute.
 * This doesn't change the representation in the kernel.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_set_current_value(FwupdBiosSetting *self, const gchar *value)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->current_value, value) == 0)
		return;

	g_free(priv->current_value);
	priv->current_value = g_strdup(value);
}

static gboolean
fwupd_bios_setting_trusted(FwupdBiosSetting *self, gboolean trusted)
{
	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), FALSE);

	if (trusted)
		return TRUE;
	if (g_strcmp0(fwupd_bios_setting_get_name(self), "pending_reboot") == 0)
		return TRUE;
	return FALSE;
}

/**
 * fwupd_bios_setting_to_variant:
 * @self: a #FwupdBiosSetting
 * @trusted: whether the caller should receive trusted values
 *
 * Serialize the bios setting.
 *
 * Returns: the serialized data, or %NULL for error.
 *
 * Since: 1.8.4
 **/
GVariant *
fwupd_bios_setting_to_variant(FwupdBiosSetting *self, gboolean trusted)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;

	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder,
			      "{sv}",
			      FWUPD_RESULT_KEY_BIOS_SETTING_TYPE,
			      g_variant_new_uint64(priv->kind));
	if (priv->id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BIOS_SETTING_ID,
				      g_variant_new_string(priv->id));
	}
	if (priv->name != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_NAME,
				      g_variant_new_string(priv->name));
	}
	if (priv->path != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FILENAME,
				      g_variant_new_string(priv->path));
	}
	if (priv->description != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DESCRIPTION,
				      g_variant_new_string(priv->description));
	}
	g_variant_builder_add(&builder,
			      "{sv}",
			      FWUPD_RESULT_KEY_BIOS_SETTING_READ_ONLY,
			      g_variant_new_boolean(priv->read_only));
	if (fwupd_bios_setting_trusted(self, trusted)) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BIOS_SETTING_CURRENT_VALUE,
				      g_variant_new_string(priv->current_value));
	}
	if (priv->kind == FWUPD_BIOS_SETTING_KIND_INTEGER ||
	    priv->kind == FWUPD_BIOS_SETTING_KIND_STRING) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BIOS_SETTING_LOWER_BOUND,
				      g_variant_new_uint64(priv->lower_bound));
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BIOS_SETTING_UPPER_BOUND,
				      g_variant_new_uint64(priv->upper_bound));
		if (priv->kind == FWUPD_BIOS_SETTING_KIND_INTEGER) {
			g_variant_builder_add(&builder,
					      "{sv}",
					      FWUPD_RESULT_KEY_BIOS_SETTING_SCALAR_INCREMENT,
					      g_variant_new_uint64(priv->scalar_increment));
		}
	} else if (priv->kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		if (priv->possible_values->len > 0) {
			g_autofree const gchar **strv =
			    g_new0(const gchar *, priv->possible_values->len + 1);
			for (guint i = 0; i < priv->possible_values->len; i++)
				strv[i] =
				    (const gchar *)g_ptr_array_index(priv->possible_values, i);
			g_variant_builder_add(&builder,
					      "{sv}",
					      FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES,
					      g_variant_new_strv(strv, -1));
		}
	}
	return g_variant_new("a{sv}", &builder);
}

static void
fwupd_bios_setting_from_key_value(FwupdBiosSetting *self, const gchar *key, GVariant *value)
{
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_TYPE) == 0) {
		fwupd_bios_setting_set_kind(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_ID) == 0) {
		fwupd_bios_setting_set_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_bios_setting_set_name(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FILENAME) == 0) {
		fwupd_bios_setting_set_path(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_CURRENT_VALUE) == 0) {
		fwupd_bios_setting_set_current_value(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DESCRIPTION) == 0) {
		fwupd_bios_setting_set_description(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_bios_setting_add_possible_value(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_LOWER_BOUND) == 0) {
		fwupd_bios_setting_set_lower_bound(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_UPPER_BOUND) == 0) {
		fwupd_bios_setting_set_upper_bound(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_SCALAR_INCREMENT) == 0) {
		fwupd_bios_setting_set_scalar_increment(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_SETTING_READ_ONLY) == 0) {
		fwupd_bios_setting_set_read_only(self, g_variant_get_boolean(value));
		return;
	}
}

/**
 * fwupd_bios_setting_from_json:
 * @self: a #FwupdBiosSetting
 * @json_node: a JSON node
 * @error: (nullable): optional return location for an error
 *
 * Loads a fwupd bios setting from a JSON node.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_bios_setting_from_json(FwupdBiosSetting *self, JsonNode *json_node, GError **error)
{
#if JSON_CHECK_VERSION(1, 6, 0)
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}
	obj = json_node_get_object(json_node);

	fwupd_bios_setting_set_kind(
	    self,
	    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_BIOS_SETTING_TYPE, 0));
	fwupd_bios_setting_set_id(
	    self,
	    json_object_get_string_member_with_default(obj,
						       FWUPD_RESULT_KEY_BIOS_SETTING_ID,
						       NULL));

	fwupd_bios_setting_set_name(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_NAME, NULL));
	fwupd_bios_setting_set_description(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_DESCRIPTION, NULL));
	fwupd_bios_setting_set_path(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_FILENAME, NULL));
	fwupd_bios_setting_set_current_value(
	    self,
	    json_object_get_string_member_with_default(obj,
						       FWUPD_RESULT_KEY_BIOS_SETTING_CURRENT_VALUE,
						       NULL));
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES)) {
		JsonArray *array =
		    json_object_get_array_member(obj,
						 FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES);
		for (guint i = 0; i < json_array_get_length(array); i++) {
			const gchar *tmp = json_array_get_string_element(array, i);
			fwupd_bios_setting_add_possible_value(self, tmp);
		}
	}
	fwupd_bios_setting_set_lower_bound(
	    self,
	    json_object_get_int_member_with_default(obj,
						    FWUPD_RESULT_KEY_BIOS_SETTING_LOWER_BOUND,
						    0));
	fwupd_bios_setting_set_upper_bound(
	    self,
	    json_object_get_int_member_with_default(obj,
						    FWUPD_RESULT_KEY_BIOS_SETTING_UPPER_BOUND,
						    0));
	fwupd_bios_setting_set_scalar_increment(
	    self,
	    json_object_get_int_member_with_default(obj,
						    FWUPD_RESULT_KEY_BIOS_SETTING_SCALAR_INCREMENT,
						    0));
	fwupd_bios_setting_set_read_only(
	    self,
	    json_object_get_int_member_with_default(obj,
						    FWUPD_RESULT_KEY_BIOS_SETTING_READ_ONLY,
						    0));
	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "json-glib version too old");
	return FALSE;
#endif
}

/**
 * fwupd_bios_setting_to_json:
 * @self: a #FwupdBiosSetting
 * @builder: a JSON builder
 *
 * Adds a fwupd bios setting to a JSON builder.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_setting_to_json(FwupdBiosSetting *self, JsonBuilder *builder)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_BIOS_SETTING(self));
	g_return_if_fail(builder != NULL);

	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_FILENAME, priv->path);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_BIOS_SETTING_ID, priv->id);
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_BIOS_SETTING_CURRENT_VALUE,
				     priv->current_value);
	fwupd_common_json_add_boolean(builder,
				      FWUPD_RESULT_KEY_BIOS_SETTING_READ_ONLY,
				      priv->read_only);
	fwupd_common_json_add_int(builder, FWUPD_RESULT_KEY_BIOS_SETTING_TYPE, priv->kind);
	if (priv->kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		if (priv->possible_values->len > 0) {
			json_builder_set_member_name(builder,
						     FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES);
			json_builder_begin_array(builder);
			for (guint i = 0; i < priv->possible_values->len; i++) {
				const gchar *tmp = g_ptr_array_index(priv->possible_values, i);
				json_builder_add_string_value(builder, tmp);
			}
			json_builder_end_array(builder);
		}
	}
	if (priv->kind == FWUPD_BIOS_SETTING_KIND_INTEGER ||
	    priv->kind == FWUPD_BIOS_SETTING_KIND_STRING) {
		fwupd_common_json_add_int(builder,
					  FWUPD_RESULT_KEY_BIOS_SETTING_LOWER_BOUND,
					  priv->lower_bound);
		fwupd_common_json_add_int(builder,
					  FWUPD_RESULT_KEY_BIOS_SETTING_UPPER_BOUND,
					  priv->upper_bound);
		if (priv->kind == FWUPD_BIOS_SETTING_KIND_INTEGER) {
			fwupd_common_json_add_int(builder,
						  FWUPD_RESULT_KEY_BIOS_SETTING_SCALAR_INCREMENT,
						  priv->scalar_increment);
		}
	}
}

/**
 * fwupd_bios_setting_to_string:
 * @self: a #FwupdBiosSetting
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.8.4
 **/
gchar *
fwupd_bios_setting_to_string(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	GString *str;

	g_return_val_if_fail(FWUPD_IS_BIOS_SETTING(self), NULL);

	str = g_string_new(NULL);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_BIOS_SETTING_ID, priv->id);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_FILENAME, priv->path);
	fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_BIOS_SETTING_TYPE, priv->kind);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_BIOS_SETTING_CURRENT_VALUE, priv->current_value);
	fwupd_pad_kv_str(str,
			 FWUPD_RESULT_KEY_BIOS_SETTING_READ_ONLY,
			 priv->read_only ? "True" : "False");

	if (priv->kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		for (guint i = 0; i < priv->possible_values->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->possible_values, i);
			fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES, tmp);
		}
	}
	if (priv->kind == FWUPD_BIOS_SETTING_KIND_INTEGER ||
	    priv->kind == FWUPD_BIOS_SETTING_KIND_STRING) {
		fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_BIOS_SETTING_LOWER_BOUND, priv->lower_bound);
		fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_BIOS_SETTING_UPPER_BOUND, priv->upper_bound);
		if (priv->kind == FWUPD_BIOS_SETTING_KIND_INTEGER) {
			fwupd_pad_kv_int(str,
					 FWUPD_RESULT_KEY_BIOS_SETTING_SCALAR_INCREMENT,
					 priv->scalar_increment);
		}
	}

	return g_string_free(str, FALSE);
}

static void
fwupd_bios_setting_class_init(FwupdBiosSettingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_bios_setting_finalize;
}

static void
fwupd_bios_setting_init(FwupdBiosSetting *self)
{
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);
	priv->possible_values = g_ptr_array_new_with_free_func(g_free);
}

static void
fwupd_bios_setting_finalize(GObject *object)
{
	FwupdBiosSetting *self = FWUPD_BIOS_SETTING(object);
	FwupdBiosSettingPrivate *priv = GET_PRIVATE(self);

	g_free(priv->current_value);
	g_free(priv->id);
	g_free(priv->name);
	g_free(priv->description);
	g_free(priv->path);
	g_ptr_array_unref(priv->possible_values);

	G_OBJECT_CLASS(fwupd_bios_setting_parent_class)->finalize(object);
}

static void
fwupd_bios_setting_set_from_variant_iter(FwupdBiosSetting *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_bios_setting_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

/**
 * fwupd_bios_setting_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new bios setting using serialized data.
 *
 * Returns: (transfer full): a new #FwupdBiosSetting, or %NULL if @value was invalid.
 *
 * Since: 1.8.4
 **/
FwupdBiosSetting *
fwupd_bios_setting_from_variant(GVariant *value)
{
	FwupdBiosSetting *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	g_return_val_if_fail(value != NULL, NULL);

	type_string = g_variant_get_type_string(value);
	if (g_strcmp0(type_string, "(a{sv})") == 0) {
		rel = g_object_new(FWUPD_TYPE_BIOS_SETTING, NULL);
		g_variant_get(value, "(a{sv})", &iter);
		fwupd_bios_setting_set_from_variant_iter(rel, iter);
	} else if (g_strcmp0(type_string, "a{sv}") == 0) {
		rel = g_object_new(FWUPD_TYPE_BIOS_SETTING, NULL);
		g_variant_get(value, "a{sv}", &iter);
		fwupd_bios_setting_set_from_variant_iter(rel, iter);
	} else {
		g_warning("type %s not known", type_string);
	}
	return rel;
}

/**
 * fwupd_bios_setting_array_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates an array of new bios settings using serialized data.
 *
 * Returns: (transfer container) (element-type FwupdBiosSetting): attributes,
 * or %NULL if @value was invalid.
 *
 * Since: 1.8.4
 **/
GPtrArray *
fwupd_bios_setting_array_from_variant(GVariant *value)
{
	GPtrArray *array = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	untuple = g_variant_get_child_value(value, 0);
	sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		FwupdBiosSetting *rel;
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value(untuple, i);
		rel = fwupd_bios_setting_from_variant(data);
		if (rel == NULL)
			continue;
		g_ptr_array_add(array, rel);
	}
	return array;
}

/**
 * fwupd_bios_setting_new:
 * @name: (nullable): the attribute name
 * @path: (nullable): the path the driver providing this attribute uses
 *
 * Creates a new bios setting.
 *
 * Returns: a new #FwupdBiosSetting.
 *
 * Since: 1.8.4
 **/
FwupdBiosSetting *
fwupd_bios_setting_new(const gchar *name, const gchar *path)
{
	FwupdBiosSetting *self;

	self = g_object_new(FWUPD_TYPE_BIOS_SETTING, NULL);
	if (name != NULL)
		fwupd_bios_setting_set_name(self, name);
	if (path != NULL)
		fwupd_bios_setting_set_path(self, path);

	return FWUPD_BIOS_SETTING(self);
}
