/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-bios-attr-private.h"
#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"

/**
 * FwupdBiosAttr:
 *
 * A BIOS attribute that represents a setting in the firmware.
 */

static void
fwupd_bios_attr_finalize(GObject *object);

typedef struct {
	FwupdBiosAttrKind kind;
	gchar *name;
	gchar *description;
	gchar *path;
	gchar *current_value;
	guint64 lower_bound;
	guint64 upper_bound;
	guint64 scalar_increment;
	gboolean read_only;
	GPtrArray *possible_values;
} FwupdBiosAttrPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FwupdBiosAttr, fwupd_bios_attr, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_bios_attr_get_instance_private(o))

/**
 * fwupd_bios_attr_get_read_only:
 * @self: a #FwupdBiosAttr
 *
 * Determines if a BIOS attribute is read only
 *
 * Returns: gboolean
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_bios_attr_get_read_only(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), FALSE);
	return priv->read_only;
}

/**
 * fwupd_bios_attr_set_read_only:
 * @self: a #FwupdBiosAttr
 *
 * Configures whether an attribute is read only
 * maximum length for string attributes.
 *
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_read_only(FwupdBiosAttr *self, gboolean val)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));
	priv->read_only = val;
}

/**
 * fwupd_bios_attr_get_lower_bound:
 * @self: a #FwupdBiosAttr
 *
 * Gets the lower bound for integer attributes or
 * minimum length for string attributes.
 *
 * Returns: guint64
 *
 * Since: 1.8.4
 **/
guint64
fwupd_bios_attr_get_lower_bound(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), 0);
	return priv->lower_bound;
}

/**
 * fwupd_bios_attr_get_upper_bound:
 * @self: a #FwupdBiosAttr
 *
 * Gets the upper bound for integer attributes or
 * maximum length for string attributes.
 *
 * Returns: guint64
 *
 * Since: 1.8.4
 **/
guint64
fwupd_bios_attr_get_upper_bound(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), 0);
	return priv->upper_bound;
}

/**
 * fwupd_bios_attr_get_scalar_increment:
 * @self: a #FwupdBiosAttr
 *
 * Gets the scalar increment used for integer attributes.
 *
 * Returns: guint64
 *
 * Since: 1.8.4
 **/
guint64
fwupd_bios_attr_get_scalar_increment(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), 0);
	return priv->scalar_increment;
}

/**
 * fwupd_bios_attr_set_upper_bound:
 * @self: a #FwupdBiosAttr
 * @val: a guint64 value to set bound to
 *
 * Sets the upper bound used for BIOS integer attributes or max
 * length for string attributes.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_upper_bound(FwupdBiosAttr *self, guint64 val)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));
	priv->upper_bound = val;
}

/**
 * fwupd_bios_attr_set_lower_bound:
 * @self: a #FwupdBiosAttr
 * @val: a guint64 value to set bound to
 *
 * Sets the lower bound used for BIOS integer attributes or max
 * length for string attributes.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_lower_bound(FwupdBiosAttr *self, guint64 val)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));
	priv->lower_bound = val;
}

/**
 * fwupd_bios_attr_set_scalar_increment:
 * @self: a #FwupdBiosAttr
 * @val: a guint64 value to set increment to
 *
 * Sets the scalar increment used for BIOS integer attributes.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_scalar_increment(FwupdBiosAttr *self, guint64 val)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));
	priv->scalar_increment = val;
}

/**
 * fwupd_bios_attr_get_kind:
 * @self: a #FwupdBiosAttr
 *
 * Gets the BIOS attribute type used by the kernel interface.
 *
 * Returns: the bios attribute type, or %FWUPD_BIOS_ATTR_KIND_UNKNOWN if unset.
 *
 * Since: 1.8.4
 **/
FwupdBiosAttrKind
fwupd_bios_attr_get_kind(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), 0);
	return priv->kind;
}

/**
 * fwupd_bios_attr_set_kind:
 * @self: a #FwupdBiosAttr
 * @type: a bios attribute type, e.g. %FWUPD_BIOS_ATTR_KIND_ENUMERATION
 *
 * Sets the BIOS attribute type used by the kernel interface.
 * Setting a @type of %FWUPD_BIOS_ATTR_KIND_UNKNOWN is not supported.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_kind(FwupdBiosAttr *self, FwupdBiosAttrKind type)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));
	g_return_if_fail(type != FWUPD_BIOS_ATTR_KIND_UNKNOWN);
	priv->kind = type;
}

/**
 * fwupd_bios_attr_set_name:
 * @self: a #FwupdBiosAttr
 * @name: (nullable): the attribute name
 *
 * Sets the attribute name provided by a kernel driver.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_name(FwupdBiosAttr *self, const gchar *name)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->name, name) == 0)
		return;

	g_free(priv->name);
	priv->name = g_strdup(name);
}

/**
 * fwupd_bios_attr_set_path:
 * @self: a #FwupdBiosAttr
 * @path: (nullable): the path the driver providing the attribute uses
 *
 * Sets path to the attribute.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_path(FwupdBiosAttr *self, const gchar *path)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->path, path) == 0)
		return;

	g_free(priv->path);
	priv->path = g_strdup(path);
}

/**
 * fwupd_bios_attr_set_description:
 * @self: a #FwupdBiosAttr
 * @description: (nullable): the attribute description
 *
 * Sets the attribute description.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_description(FwupdBiosAttr *self, const gchar *description)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));

	/* not changed */
	if (g_strcmp0(priv->description, description) == 0)
		return;

	g_free(priv->description);
	priv->description = g_strdup(description);
}

/**
 * fwupd_bios_attr_has_possible_value:
 * @self: a #FwupdBiosAttr
 * @val: the possible value string
 *
 * Finds out if a specific possible value was added to the attribute.
 *
 * Returns: %TRUE if the self matches.
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_bios_attr_has_possible_value(FwupdBiosAttr *self, const gchar *val)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), FALSE);
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
 * fwupd_bios_attr_add_possible_value:
 * @self: a #FwupdBiosAttr
 * @possible_value: the possible
 *
 * Adds a possible value to the attribute.  This indicates one of the values the
 * kernel driver will accept from userspace.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_add_possible_value(FwupdBiosAttr *self, const gchar *possible_value)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));
	if (priv->possible_values->len > 0 &&
	    fwupd_bios_attr_has_possible_value(self, possible_value))
		return;
	g_ptr_array_add(priv->possible_values, g_strdup(possible_value));
}

/**
 * fwupd_bios_attr_get_possible_values:
 * @self: a #FwupdBiosAttr
 *
 * Find all possible values for an enumeration attribute.
 *
 * Returns: (transfer container) (element-type gchar*): all possible values.
 *
 * Since: 1.8.4
 **/
GPtrArray *
fwupd_bios_attr_get_possible_values(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), NULL);
	g_return_val_if_fail(priv->kind == FWUPD_BIOS_ATTR_KIND_ENUMERATION, NULL);
	return priv->possible_values;
}

/**
 * fwupd_bios_attr_get_name:
 * @self: a #FwupdBiosAttr
 *
 * Gets the attribute name.
 *
 * Returns: the attribute name, or %NULL if unset.
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_attr_get_name(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), NULL);
	return priv->name;
}

/**
 * fwupd_bios_attr_get_path:
 * @self: a #FwupdBiosAttr
 *
 * Gets the path for the driver providing the attribute.
 *
 * Returns: (nullable): the driver, or %NULL if unfound.
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_attr_get_path(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), NULL);
	return priv->path;
}

/**
 * fwupd_bios_attr_get_description:
 * @self: a #FwupdBiosAttr
 *
 * Gets the attribute description which is provided by some drivers to explain
 * what they change.
 *
 * Returns: the attribute description, or %NULL if unset.
 *
 * Since: 1.8.4
 **/
const gchar *
fwupd_bios_attr_get_description(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), NULL);
	return priv->description;
}

/**
 * fwupd_bios_attr_get_current_value:
 * @self: a #FwupdBiosAttr
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
fwupd_bios_attr_get_current_value(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), NULL);
	return priv->current_value;
}

/**
 * fwupd_bios_attr_set_current_value:
 * @self: a #FwupdBiosAttr
 * @value: The string to set an attribute to
 *
 * Sets the string stored in an attribute.
 * This doesn't change the representation in the kernel.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_set_current_value(FwupdBiosAttr *self, const gchar *value)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->current_value, value) == 0)
		return;

	g_free(priv->current_value);
	priv->current_value = g_strdup(value);
}

/**
 * fwupd_bios_attr_to_variant:
 * @self: a #FwupdBiosAttr
 *
 * Serialize the bios attribute.
 *
 * Returns: the serialized data, or %NULL for error.
 *
 * Since: 1.8.4
 **/
GVariant *
fwupd_bios_attr_to_variant(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;

	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder,
			      "{sv}",
			      FWUPD_RESULT_KEY_BIOS_ATTR_TYPE,
			      g_variant_new_uint64(priv->kind));
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
			      FWUPD_RESULT_KEY_BIOS_ATTR_CURRENT_VALUE,
			      g_variant_new_string(priv->current_value));
	if (priv->kind == FWUPD_BIOS_ATTR_KIND_INTEGER ||
	    priv->kind == FWUPD_BIOS_ATTR_KIND_STRING) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BIOS_ATTR_LOWER_BOUND,
				      g_variant_new_uint64(priv->lower_bound));
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BIOS_ATTR_UPPER_BOUND,
				      g_variant_new_uint64(priv->upper_bound));
		if (priv->kind == FWUPD_BIOS_ATTR_KIND_INTEGER) {
			g_variant_builder_add(&builder,
					      "{sv}",
					      FWUPD_RESULT_KEY_BIOS_ATTR_SCALAR_INCREMENT,
					      g_variant_new_uint64(priv->scalar_increment));
		}
	} else if (priv->kind == FWUPD_BIOS_ATTR_KIND_ENUMERATION) {
		if (priv->possible_values->len > 0) {
			g_autofree const gchar **strv =
			    g_new0(const gchar *, priv->possible_values->len + 1);
			for (guint i = 0; i < priv->possible_values->len; i++)
				strv[i] =
				    (const gchar *)g_ptr_array_index(priv->possible_values, i);
			g_variant_builder_add(&builder,
					      "{sv}",
					      FWUPD_RESULT_KEY_BIOS_ATTR_POSSIBLE_VALUES,
					      g_variant_new_strv(strv, -1));
		}
	}
	return g_variant_new("a{sv}", &builder);
}

static void
fwupd_bios_attr_from_key_value(FwupdBiosAttr *self, const gchar *key, GVariant *value)
{
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_ATTR_TYPE) == 0) {
		fwupd_bios_attr_set_kind(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_bios_attr_set_name(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FILENAME) == 0) {
		fwupd_bios_attr_set_path(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_ATTR_CURRENT_VALUE) == 0) {
		fwupd_bios_attr_set_current_value(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DESCRIPTION) == 0) {
		fwupd_bios_attr_set_description(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_ATTR_POSSIBLE_VALUES) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_bios_attr_add_possible_value(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_ATTR_LOWER_BOUND) == 0) {
		fwupd_bios_attr_set_lower_bound(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_ATTR_UPPER_BOUND) == 0) {
		fwupd_bios_attr_set_upper_bound(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_ATTR_SCALAR_INCREMENT) == 0) {
		fwupd_bios_attr_set_scalar_increment(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BIOS_ATTR_READ_ONLY) == 0) {
		fwupd_bios_attr_set_read_only(self, g_variant_get_boolean(value));
		return;
	}
}

/**
 * fwupd_bios_attr_from_json:
 * @self: a #FwupdBiosAttr
 * @json_node: a JSON node
 * @error: (nullable): optional return location for an error
 *
 * Loads a fwupd bios attribute from a JSON node.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_bios_attr_from_json(FwupdBiosAttr *self, JsonNode *json_node, GError **error)
{
#if JSON_CHECK_VERSION(1, 6, 0)
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}
	obj = json_node_get_object(json_node);

	fwupd_bios_attr_set_kind(
	    self,
	    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_BIOS_ATTR_TYPE, 0));
	fwupd_bios_attr_set_name(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_NAME, NULL));
	fwupd_bios_attr_set_description(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_DESCRIPTION, NULL));
	fwupd_bios_attr_set_path(
	    self,
	    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_FILENAME, NULL));
	fwupd_bios_attr_set_current_value(
	    self,
	    json_object_get_string_member_with_default(obj,
						       FWUPD_RESULT_KEY_BIOS_ATTR_CURRENT_VALUE,
						       NULL));

	if (json_object_has_member(obj, FWUPD_RESULT_KEY_BIOS_ATTR_POSSIBLE_VALUES)) {
		JsonArray *array =
		    json_object_get_array_member(obj, FWUPD_RESULT_KEY_BIOS_ATTR_POSSIBLE_VALUES);
		for (guint i = 0; i < json_array_get_length(array); i++) {
			const gchar *tmp = json_array_get_string_element(array, i);
			fwupd_bios_attr_add_possible_value(self, tmp);
		}
	}
	fwupd_bios_attr_set_lower_bound(
	    self,
	    json_object_get_int_member_with_default(obj,
						    FWUPD_RESULT_KEY_BIOS_ATTR_LOWER_BOUND,
						    0));
	fwupd_bios_attr_set_upper_bound(
	    self,
	    json_object_get_int_member_with_default(obj,
						    FWUPD_RESULT_KEY_BIOS_ATTR_UPPER_BOUND,
						    0));
	fwupd_bios_attr_set_scalar_increment(
	    self,
	    json_object_get_int_member_with_default(obj,
						    FWUPD_RESULT_KEY_BIOS_ATTR_SCALAR_INCREMENT,
						    0));
	fwupd_bios_attr_set_read_only(
	    self,
	    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_BIOS_ATTR_READ_ONLY, 0));
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
 * fwupd_bios_attr_to_json:
 * @self: a #FwupdBiosAttr
 * @builder: a JSON builder
 *
 * Adds a fwupd bios attribute to a JSON builder.
 *
 * Since: 1.8.4
 **/
void
fwupd_bios_attr_to_json(FwupdBiosAttr *self, JsonBuilder *builder)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_BIOS_ATTR(self));
	g_return_if_fail(builder != NULL);

	fwupd_common_json_add_boolean(builder,
				      FWUPD_RESULT_KEY_BIOS_ATTR_READ_ONLY,
				      priv->read_only);
	fwupd_common_json_add_int(builder, FWUPD_RESULT_KEY_BIOS_ATTR_TYPE, priv->kind);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_common_json_add_string(builder, FWUPD_RESULT_KEY_FILENAME, priv->path);
	fwupd_common_json_add_string(builder,
				     FWUPD_RESULT_KEY_BIOS_ATTR_CURRENT_VALUE,
				     priv->current_value);
	if (priv->kind == FWUPD_BIOS_ATTR_KIND_ENUMERATION) {
		if (priv->possible_values->len > 0) {
			json_builder_set_member_name(builder,
						     FWUPD_RESULT_KEY_BIOS_ATTR_POSSIBLE_VALUES);
			json_builder_begin_array(builder);
			for (guint i = 0; i < priv->possible_values->len; i++) {
				const gchar *tmp = g_ptr_array_index(priv->possible_values, i);
				json_builder_add_string_value(builder, tmp);
			}
			json_builder_end_array(builder);
		}
	}
	if (priv->kind == FWUPD_BIOS_ATTR_KIND_INTEGER ||
	    priv->kind == FWUPD_BIOS_ATTR_KIND_STRING) {
		fwupd_common_json_add_int(builder,
					  FWUPD_RESULT_KEY_BIOS_ATTR_LOWER_BOUND,
					  priv->lower_bound);
		fwupd_common_json_add_int(builder,
					  FWUPD_RESULT_KEY_BIOS_ATTR_UPPER_BOUND,
					  priv->upper_bound);
		if (priv->kind == FWUPD_BIOS_ATTR_KIND_INTEGER) {
			fwupd_common_json_add_int(builder,
						  FWUPD_RESULT_KEY_BIOS_ATTR_SCALAR_INCREMENT,
						  priv->scalar_increment);
		}
	}
}

/**
 * fwupd_bios_attr_to_string:
 * @self: a #FwupdBiosAttr
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.8.4
 **/
gchar *
fwupd_bios_attr_to_string(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	GString *str;

	g_return_val_if_fail(FWUPD_IS_BIOS_ATTR(self), NULL);

	str = g_string_new(NULL);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DESCRIPTION, priv->description);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_FILENAME, priv->path);
	fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_BIOS_ATTR_TYPE, priv->kind);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_BIOS_ATTR_CURRENT_VALUE, priv->current_value);
	fwupd_pad_kv_str(str,
			 FWUPD_RESULT_KEY_BIOS_ATTR_READ_ONLY,
			 priv->read_only ? "True" : "False");

	if (priv->kind == FWUPD_BIOS_ATTR_KIND_ENUMERATION) {
		for (guint i = 0; i < priv->possible_values->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->possible_values, i);
			fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_BIOS_ATTR_POSSIBLE_VALUES, tmp);
		}
	}
	if (priv->kind == FWUPD_BIOS_ATTR_KIND_INTEGER ||
	    priv->kind == FWUPD_BIOS_ATTR_KIND_STRING) {
		fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_BIOS_ATTR_LOWER_BOUND, priv->lower_bound);
		fwupd_pad_kv_int(str, FWUPD_RESULT_KEY_BIOS_ATTR_UPPER_BOUND, priv->upper_bound);
		if (priv->kind == FWUPD_BIOS_ATTR_KIND_INTEGER) {
			fwupd_pad_kv_int(str,
					 FWUPD_RESULT_KEY_BIOS_ATTR_SCALAR_INCREMENT,
					 priv->scalar_increment);
		}
	}

	return g_string_free(str, FALSE);
}

static void
fwupd_bios_attr_class_init(FwupdBiosAttrClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_bios_attr_finalize;
}

static void
fwupd_bios_attr_init(FwupdBiosAttr *self)
{
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);
	priv->possible_values = g_ptr_array_new_with_free_func(g_free);
}

static void
fwupd_bios_attr_finalize(GObject *object)
{
	FwupdBiosAttr *self = FWUPD_BIOS_ATTR(object);
	FwupdBiosAttrPrivate *priv = GET_PRIVATE(self);

	g_free(priv->current_value);
	g_free(priv->name);
	g_free(priv->description);
	g_free(priv->path);
	g_ptr_array_unref(priv->possible_values);

	G_OBJECT_CLASS(fwupd_bios_attr_parent_class)->finalize(object);
}

static void
fwupd_bios_attr_set_from_variant_iter(FwupdBiosAttr *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_bios_attr_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

/**
 * fwupd_bios_attr_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new bios attribute using serialized data.
 *
 * Returns: (transfer full): a new #FwupdBiosAttr, or %NULL if @value was invalid.
 *
 * Since: 1.8.4
 **/
FwupdBiosAttr *
fwupd_bios_attr_from_variant(GVariant *value)
{
	FwupdBiosAttr *rel = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	g_return_val_if_fail(value != NULL, NULL);

	type_string = g_variant_get_type_string(value);
	if (g_strcmp0(type_string, "(a{sv})") == 0) {
		rel = g_object_new(FWUPD_TYPE_BIOS_ATTR, NULL);
		g_variant_get(value, "(a{sv})", &iter);
		fwupd_bios_attr_set_from_variant_iter(rel, iter);
	} else if (g_strcmp0(type_string, "a{sv}") == 0) {
		rel = g_object_new(FWUPD_TYPE_BIOS_ATTR, NULL);
		g_variant_get(value, "a{sv}", &iter);
		fwupd_bios_attr_set_from_variant_iter(rel, iter);
	} else {
		g_warning("type %s not known", type_string);
	}
	return rel;
}

/**
 * fwupd_bios_attr_array_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates an array of new bios attributes using serialized data.
 *
 * Returns: (transfer container) (element-type FwupdBiosAttr): attributes,
 * or %NULL if @value was invalid.
 *
 * Since: 1.8.4
 **/
GPtrArray *
fwupd_bios_attr_array_from_variant(GVariant *value)
{
	GPtrArray *array = NULL;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	untuple = g_variant_get_child_value(value, 0);
	sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		FwupdBiosAttr *rel;
		g_autoptr(GVariant) data = NULL;
		data = g_variant_get_child_value(untuple, i);
		rel = fwupd_bios_attr_from_variant(data);
		if (rel == NULL)
			continue;
		g_ptr_array_add(array, rel);
	}
	return array;
}

/**
 * fwupd_bios_attr_new:
 * @name: (nullable): the attribute name
 * @path: (nullable): the path the driver providing this attribute uses
 *
 * Creates a new bios attribute.
 *
 * Returns: a new #FwupdBiosAttr.
 *
 * Since: 1.8.4
 **/
FwupdBiosAttr *
fwupd_bios_attr_new(const gchar *name, const gchar *path)
{
	FwupdBiosAttr *self;

	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(path != NULL, NULL);

	self = g_object_new(FWUPD_TYPE_BIOS_ATTR, NULL);
	fwupd_bios_attr_set_name(self, name);
	fwupd_bios_attr_set_path(self, path);

	return FWUPD_BIOS_ATTR(self);
}
