/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuSecurityAttrs"

#include "config.h"

#include <fwupd.h>
#include <glib/gi18n.h>

#include "fwupd-security-attr-private.h"

#include "fu-security-attrs-private.h"
#include "fu-security-attrs.h"

/**
 * FuSecurityAttrs:
 *
 * A set of Host Security ID attributes that represents the system state.
 */

struct _FuSecurityAttrs {
	GObject parent_instance;
	GPtrArray *attrs;
};

/* probably sane to *not* make this part of the ABI */
#define FWUPD_SECURITY_ATTR_ID_DOC_URL "https://fwupd.github.io/libfwupdplugin/hsi.html"

G_DEFINE_TYPE(FuSecurityAttrs, fu_security_attrs, G_TYPE_OBJECT)

static void
fu_security_attrs_finalize(GObject *obj)
{
	FuSecurityAttrs *self = FU_SECURITY_ATTRS(obj);
	g_ptr_array_unref(self->attrs);
	G_OBJECT_CLASS(fu_security_attrs_parent_class)->finalize(obj);
}

static void
fu_security_attrs_class_init(FuSecurityAttrsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_security_attrs_finalize;
}

static void
fu_security_attrs_init(FuSecurityAttrs *self)
{
	self->attrs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

/**
 * fu_security_attrs_append_internal:
 * @self: a #FuSecurityAttrs
 * @attr: a #FwupdSecurityAttr
 *
 * Adds a #FwupdSecurityAttr to the array with no sanity checks.
 *
 * Since: 1.7.1
 **/
void
fu_security_attrs_append_internal(FuSecurityAttrs *self, FwupdSecurityAttr *attr)
{
	g_return_if_fail(FU_IS_SECURITY_ATTRS(self));
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(attr));
	g_ptr_array_add(self->attrs, g_object_ref(attr));
}

/**
 * fu_security_attrs_append:
 * @self: a #FuSecurityAttrs
 * @attr: a #FwupdSecurityAttr
 *
 * Adds a #FwupdSecurityAttr to the array.
 *
 * Since: 1.5.0
 **/
void
fu_security_attrs_append(FuSecurityAttrs *self, FwupdSecurityAttr *attr)
{
	g_return_if_fail(FU_IS_SECURITY_ATTRS(self));
	g_return_if_fail(FWUPD_IS_SECURITY_ATTR(attr));

	/* sanity check */
	if (fwupd_security_attr_get_plugin(attr) == NULL) {
		g_warning("%s has no plugin set", fwupd_security_attr_get_appstream_id(attr));
	}

	/* sanity check, and correctly prefix the URLs with the current mirror */
	if (fwupd_security_attr_get_url(attr) == NULL) {
		g_autofree gchar *url = NULL;
		url = g_strdup_printf("%s#%s",
				      FWUPD_SECURITY_ATTR_ID_DOC_URL,
				      fwupd_security_attr_get_appstream_id(attr));
		fwupd_security_attr_set_url(attr, url);
	} else if (g_str_has_prefix(fwupd_security_attr_get_url(attr), "#")) {
		g_autofree gchar *url = NULL;
		url = g_strdup_printf("%s%s",
				      FWUPD_SECURITY_ATTR_ID_DOC_URL,
				      fwupd_security_attr_get_url(attr));
		fwupd_security_attr_set_url(attr, url);
	}
	fu_security_attrs_append_internal(self, attr);
}

/**
 * fu_security_attrs_get_by_appstream_id:
 * @self: a #FuSecurityAttrs
 * @appstream_id: an ID, e.g. %FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM
 *
 * Gets a specific #FwupdSecurityAttr from the array.
 *
 * Returns: (transfer full): a #FwupdSecurityAttr or %NULL
 *
 * Since: 1.7.2
 **/
FwupdSecurityAttr *
fu_security_attrs_get_by_appstream_id(FuSecurityAttrs *self, const gchar *appstream_id)
{
	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(self), NULL);
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(self->attrs, i);
		if (g_strcmp0(fwupd_security_attr_get_appstream_id(attr), appstream_id) == 0)
			return g_object_ref(attr);
	}
	return NULL;
}

/**
 * fu_security_attrs_to_variant:
 * @self: a #FuSecurityAttrs
 *
 * Serializes the #FwupdSecurityAttr objects.
 *
 * Returns: a #GVariant or %NULL
 *
 * Since: 1.5.0
 **/
GVariant *
fu_security_attrs_to_variant(FuSecurityAttrs *self)
{
	GVariantBuilder builder;

	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(self), NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *security_attr = g_ptr_array_index(self->attrs, i);
		GVariant *tmp = fwupd_security_attr_to_variant(security_attr);
		g_variant_builder_add_value(&builder, tmp);
	}
	return g_variant_new("(aa{sv})", &builder);
}

/**
 * fu_security_attrs_get_all:
 * @self: a #FuSecurityAttrs
 *
 * Gets all the attributes in the object.
 *
 * Returns: (transfer container) (element-type FwupdSecurityAttr): attributes
 *
 * Since: 1.5.0
 **/
GPtrArray *
fu_security_attrs_get_all(FuSecurityAttrs *self)
{
	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(self), NULL);
	return g_ptr_array_ref(self->attrs);
}

/**
 * fu_security_attrs_remove_all:
 * @self: a #FuSecurityAttrs
 *
 * Removes all the attributes in the object.
 *
 * Since: 1.5.0
 **/
void
fu_security_attrs_remove_all(FuSecurityAttrs *self)
{
	g_return_if_fail(FU_IS_SECURITY_ATTRS(self));
	return g_ptr_array_set_size(self->attrs, 0);
}

/**
 * fu_security_attrs_calculate_hsi:
 * @self: a #FuSecurityAttrs
 * @flags: HSI attribute flags
 *
 * Calculates the HSI string from the appended attributes.
 *
 * Returns: (transfer full): a string or %NULL
 *
 * Since: 1.5.0
 **/
gchar *
fu_security_attrs_calculate_hsi(FuSecurityAttrs *self, FuSecurityAttrsFlags flags)
{
	guint hsi_number = 0;
	FwupdSecurityAttrFlags attr_flags = FWUPD_SECURITY_ATTR_FLAG_NONE;
	g_autoptr(GString) str = g_string_new("HSI:");
	const FwupdSecurityAttrFlags hpi_suffixes[] = {
	    FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE,
	    FWUPD_SECURITY_ATTR_FLAG_NONE,
	};

	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(self), NULL);

	/* find the highest HSI number where there are no failures and at least
	 * one success */
	for (guint j = 1; j <= FWUPD_SECURITY_ATTR_LEVEL_LAST; j++) {
		gboolean success_cnt = 0;
		gboolean failure_cnt = 0;
		for (guint i = 0; i < self->attrs->len; i++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(self->attrs, i);
			if (fwupd_security_attr_get_level(attr) != j)
				continue;
			if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				success_cnt++;
			else if (!fwupd_security_attr_has_flag(attr,
							       FWUPD_SECURITY_ATTR_FLAG_OBSOLETED))
				failure_cnt++;
		}

		/* abort */
		if (failure_cnt > 0) {
			hsi_number = j - 1;
			break;
		}

		/* we matched at least one thing on this level */
		if (success_cnt > 0)
			hsi_number = j;
	}

	/* get a logical OR of the runtime flags */
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(self->attrs, i);
		if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED))
			continue;
		if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE) &&
		    fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
			continue;
		if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA))
			return g_strdup("HSI:INVALID:missing-data");

		attr_flags |= fwupd_security_attr_get_flags(attr);
	}

	g_string_append_printf(str, "%u", hsi_number);
	if (attr_flags & FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE) {
		for (guint j = 0; hpi_suffixes[j] != FWUPD_SECURITY_ATTR_FLAG_NONE; j++) {
			if (attr_flags & hpi_suffixes[j])
				g_string_append(
				    str,
				    fwupd_security_attr_flag_to_suffix(hpi_suffixes[j]));
		}
	}

	if (flags & FU_SECURITY_ATTRS_FLAG_ADD_VERSION) {
		g_string_append_printf(str,
				       " (v%d.%d.%d)",
				       FWUPD_MAJOR_VERSION,
				       FWUPD_MINOR_VERSION,
				       FWUPD_MICRO_VERSION);
	}

	return g_string_free(g_steal_pointer(&str), FALSE);
}

static gchar *
fu_security_attrs_get_sort_key(FwupdSecurityAttr *attr)
{
	GString *str = g_string_new(NULL);

	/* level */
	g_string_append_printf(str, "%u", fwupd_security_attr_get_level(attr));

	/* success -> fail -> obsoletes */
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_string_append(str, "0");
	} else if (!fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS) &&
		   !fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		g_string_append(str, "1");
	} else {
		g_string_append(str, "9");
	}

	/* prefer name, but fallback to appstream-id for tests */
	if (fwupd_security_attr_get_name(attr) != NULL) {
		g_string_append(str, fwupd_security_attr_get_name(attr));
	} else {
		g_string_append(str, fwupd_security_attr_get_appstream_id(attr));
	}
	return g_string_free(str, FALSE);
}

static gint
fu_security_attrs_sort_cb(gconstpointer item1, gconstpointer item2)
{
	FwupdSecurityAttr *attr1 = *((FwupdSecurityAttr **)item1);
	FwupdSecurityAttr *attr2 = *((FwupdSecurityAttr **)item2);
	g_autofree gchar *sort1 = fu_security_attrs_get_sort_key(attr1);
	g_autofree gchar *sort2 = fu_security_attrs_get_sort_key(attr2);
	return g_strcmp0(sort1, sort2);
}

static struct {
	const gchar *appstream_id;
	FwupdSecurityAttrLevel level;
} appstream_id_level_map[] = {
    {FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION},
    {FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL},
    {FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION},
    {FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL},
    {FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL},
    {FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL},
    {FWUPD_SECURITY_ATTR_ID_INTEL_SMAP, FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION},
    {FWUPD_SECURITY_ATTR_ID_IOMMU, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_MEI_VERSION, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL},
    {FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_SPI_BLE, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL},
    {FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL},
    {FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_UEFI_PK, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
    {FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT},
    {NULL, FWUPD_SECURITY_ATTR_LEVEL_NONE}};

static void
fu_security_attrs_ensure_level(FwupdSecurityAttr *attr)
{
	const gchar *appstream_id = fwupd_security_attr_get_appstream_id(attr);

	/* already set */
	if (fwupd_security_attr_get_level(attr) != FWUPD_SECURITY_ATTR_LEVEL_NONE)
		return;

	/* not required */
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE))
		return;

	/* map ID to level in one place */
	for (guint i = 0; appstream_id_level_map[i].appstream_id != NULL; i++) {
		if (g_strcmp0(appstream_id, appstream_id_level_map[i].appstream_id) == 0) {
			fwupd_security_attr_set_level(attr, appstream_id_level_map[i].level);
			return;
		}
	}

	/* somebody forgot to add to the level map... */
	g_warning("cannot map %s to a HSI level, assuming critical", appstream_id);
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
}

/**
 * fu_security_attrs_depsolve:
 * @self: a #FuSecurityAttrs
 *
 * Marks any attributes with %FWUPD_SECURITY_ATTR_FLAG_OBSOLETED that have been
 * defined as obsoleted by other attributes.
 *
 * It is only required to call this function once, and should be done when all
 * attributes have been added. This will also sort the attrs.
 *
 * Since: 1.5.0
 **/
void
fu_security_attrs_depsolve(FuSecurityAttrs *self)
{
	g_return_if_fail(FU_IS_SECURITY_ATTRS(self));

	/* assign HSI levels if not already done */
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(self->attrs, i);
		fu_security_attrs_ensure_level(attr);
	}

	/* set flat where required */
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(self->attrs, i);
		const gchar *attr_id = fwupd_security_attr_get_appstream_id(attr);
		const gchar *attr_plugin = fwupd_security_attr_get_plugin(attr);
		GPtrArray *obsoletes = fwupd_security_attr_get_obsoletes(attr);

		for (guint j = 0; j < self->attrs->len; j++) {
			FwupdSecurityAttr *attr_tmp = g_ptr_array_index(self->attrs, j);
			const gchar *attr_tmp_id = fwupd_security_attr_get_appstream_id(attr_tmp);
			const gchar *attr_tmp_plugin = fwupd_security_attr_get_plugin(attr_tmp);

			/* skip self */
			if (g_strcmp0(attr_plugin, attr_tmp_plugin) == 0 &&
			    g_strcmp0(attr_id, attr_tmp_id) == 0)
				continue;

			/* add duplicate (negative) attributes when obsolete not explicitly set
			 */
			if (obsoletes->len == 0) {
				if (g_strcmp0(attr_id, attr_tmp_id) != 0)
					continue;
				if (fwupd_security_attr_has_flag(attr,
								 FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
					continue;
				if (fwupd_security_attr_has_flag(attr_tmp,
								 FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
					continue;

				if (fwupd_security_attr_has_obsolete(attr, attr_id))
					continue;
				if (fwupd_security_attr_has_obsolete(attr_tmp, attr_id))
					continue;
				g_debug("duplicate security attr %s from plugin %s implicitly "
					"obsoleted by plugin %s",
					attr_id,
					attr_plugin,
					attr_tmp_plugin);
				fwupd_security_attr_add_obsolete(attr, attr_id);
			}

			/* walk all the obsoletes for matches appstream ID or plugin */
			for (guint k = 0; k < obsoletes->len; k++) {
				const gchar *obsolete = g_ptr_array_index(obsoletes, k);

				if (g_strcmp0(attr_tmp_id, obsolete) == 0 ||
				    g_strcmp0(attr_tmp_plugin, obsolete) == 0) {
					g_debug("security attr %s:%s obsoleted by %s:%s",
						attr_tmp_id,
						attr_tmp_plugin,
						attr_id,
						attr_plugin);
					fwupd_security_attr_add_flag(
					    attr_tmp,
					    FWUPD_SECURITY_ATTR_FLAG_OBSOLETED);
				}
			}
		}
	}

	/* sort */
	g_ptr_array_sort(self->attrs, fu_security_attrs_sort_cb);
}

static void
fu_security_attrs_to_json(FuSecurityAttrs *self, JsonBuilder *builder)
{
	g_autoptr(GPtrArray) items = NULL;

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "SecurityAttributes");
	json_builder_begin_array(builder);
	items = fu_security_attrs_get_all(self);
	for (guint i = 0; i < items->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(items, i);
		guint64 created = fwupd_security_attr_get_created(attr);
		json_builder_begin_object(builder);
		fwupd_security_attr_set_created(attr, 0);
		fwupd_security_attr_to_json(attr, builder);
		fwupd_security_attr_set_created(attr, created);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
}

/**
 * fu_security_attrs_to_json_string:
 * @self: a pointer for a FuSecurityAttrs data structure.
 * @error: (nullable): optional return location for an error
 *
 * Convert security attribute to JSON string. e.g.:
 *    {
 *      "SecurityAttributes": [
 *        {
 *          "name": "aaa",
 *          "value": "bbb"
 *        }
 *      ]
 *    }
 *
 * Returns: (transfer full): JSON string
 *
 * Since: 1.9.2
 */
gchar *
fu_security_attrs_to_json_string(FuSecurityAttrs *self, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) json_root = NULL;

	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	fu_security_attrs_to_json(self, builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to convert security attribute to json.");
		return NULL;
	}
	return g_steal_pointer(&data);
}

/**
 * fu_security_attrs_from_json:
 * @self: a #FuSecurityAttrs
 * @json_node: a #JsonNode
 * @error: (nullable): optional return location for an error
 *
 * Imports a JSON node.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.9.2
 */
gboolean
fu_security_attrs_from_json(FuSecurityAttrs *self, JsonNode *json_node, GError **error)
{
	JsonArray *array;
	JsonObject *obj;

	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(self), FALSE);
	g_return_val_if_fail(json_node != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}
	obj = json_node_get_object(json_node);

	/* this has to exist */
	if (!json_object_has_member(obj, "SecurityAttributes")) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "no SecurityAttributes property in object");
		return FALSE;
	}
	array = json_object_get_array_member(obj, "SecurityAttributes");
	for (guint i = 0; i < json_array_get_length(array); i++) {
		JsonNode *node_tmp = json_array_get_element(array, i);
		g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_new(NULL);
		if (!fwupd_security_attr_from_json(attr, node_tmp, error))
			return FALSE;
		fu_security_attrs_append(self, attr);
	}

	/* success */
	return TRUE;
}

/**
 * fu_security_attrs_compare:
 * @attrs1: a #FuSecurityAttrs
 * @attrs2: another #FuSecurityAttrs, perhaps newer in some way
 *
 * Compares the two objects, returning the differences.
 *
 * If the two sets of attrs are considered the same then an empty array is returned.
 * Only the AppStream ID results are compared, extra metadata is ignored.
 *
 * Returns: (element-type FwupdSecurityAttr) (transfer container): differences
 *
 * Since: 1.9.2
 */
GPtrArray *
fu_security_attrs_compare(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2)
{
	g_autoptr(GHashTable) hash1 = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GHashTable) hash2 = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GPtrArray) array1 = fu_security_attrs_get_all(attrs1);
	g_autoptr(GPtrArray) array2 = fu_security_attrs_get_all(attrs2);
	g_autoptr(GPtrArray) results =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(attrs1), NULL);
	g_return_val_if_fail(FU_IS_SECURITY_ATTRS(attrs2), NULL);

	/* create hash tables of appstream-id -> FwupdSecurityAttr */
	for (guint i = 0; i < array1->len; i++) {
		FwupdSecurityAttr *attr1 = g_ptr_array_index(array1, i);
		g_hash_table_insert(hash1,
				    (gpointer)fwupd_security_attr_get_appstream_id(attr1),
				    (gpointer)attr1);
	}
	for (guint i = 0; i < array2->len; i++) {
		FwupdSecurityAttr *attr2 = g_ptr_array_index(array2, i);
		g_hash_table_insert(hash2,
				    (gpointer)fwupd_security_attr_get_appstream_id(attr2),
				    (gpointer)attr2);
	}

	/* present in attrs2, not present in attrs1 */
	for (guint i = 0; i < array2->len; i++) {
		FwupdSecurityAttr *attr1;
		FwupdSecurityAttr *attr2 = g_ptr_array_index(array2, i);
		attr1 = g_hash_table_lookup(hash1, fwupd_security_attr_get_appstream_id(attr2));
		if (attr1 == NULL) {
			g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_copy(attr2);
			g_ptr_array_add(results, g_steal_pointer(&attr));
			continue;
		}
	}

	/* present in attrs1, not present in attrs2 */
	for (guint i = 0; i < array1->len; i++) {
		FwupdSecurityAttr *attr1 = g_ptr_array_index(array1, i);
		FwupdSecurityAttr *attr2;
		attr2 = g_hash_table_lookup(hash2, fwupd_security_attr_get_appstream_id(attr1));
		if (attr2 == NULL) {
			g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_copy(attr1);
			fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
			fwupd_security_attr_set_result_fallback(
			    attr, /* flip these around */
			    fwupd_security_attr_get_result(attr1));
			g_ptr_array_add(results, g_steal_pointer(&attr));
			continue;
		}
	}

	/* find any attributes that differ */
	for (guint i = 0; i < array2->len; i++) {
		FwupdSecurityAttr *attr1;
		FwupdSecurityAttr *attr2 = g_ptr_array_index(array2, i);
		attr1 = g_hash_table_lookup(hash1, fwupd_security_attr_get_appstream_id(attr2));
		if (attr1 == NULL)
			continue;

		/* result of specific attr differed */
		if (fwupd_security_attr_get_result(attr1) !=
		    fwupd_security_attr_get_result(attr2)) {
			g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_copy(attr1);
			fwupd_security_attr_set_result(attr, fwupd_security_attr_get_result(attr2));
			fwupd_security_attr_set_result_fallback(
			    attr,
			    fwupd_security_attr_get_result(attr1));
			fwupd_security_attr_set_flags(attr, fwupd_security_attr_get_flags(attr2));
			g_ptr_array_add(results, g_steal_pointer(&attr));
		}
	}

	/* success */
	return g_steal_pointer(&results);
}

/**
 * fu_security_attrs_equal:
 * @attrs1: a #FuSecurityAttrs
 * @attrs2: another #FuSecurityAttrs
 *
 * Tests the objects for equality. Only the AppStream ID results are compared, extra metadata
 * is ignored.
 *
 * Returns: %TRUE if the set of attrs can be considered equal
 *
 * Since: 1.9.2
 */
gboolean
fu_security_attrs_equal(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2)
{
	g_autoptr(GPtrArray) compare = fu_security_attrs_compare(attrs1, attrs2);
	return compare->len == 0;
}

/**
 * fu_security_attrs_new:
 *
 * Returns: a security attribute
 *
 * Since: 1.5.0
 **/
FuSecurityAttrs *
fu_security_attrs_new(void)
{
	return g_object_new(FU_TYPE_SECURITY_ATTRS, NULL);
}
