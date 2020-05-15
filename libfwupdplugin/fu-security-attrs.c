/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuSecurityAttrs"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-security-attrs-private.h"

struct _FuSecurityAttrs {
	GObject			 parent_instance;
	GPtrArray		*attrs;
};

G_DEFINE_TYPE (FuSecurityAttrs, fu_security_attrs, G_TYPE_OBJECT)

static void
fu_security_attrs_finalize (GObject *obj)
{
	FuSecurityAttrs *self = FU_SECURITY_ATTRS (obj);
	g_ptr_array_unref (self->attrs);
	G_OBJECT_CLASS (fu_security_attrs_parent_class)->finalize (obj);
}

static void
fu_security_attrs_class_init (FuSecurityAttrsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_security_attrs_finalize;
}

static void
fu_security_attrs_init (FuSecurityAttrs *self)
{
	self->attrs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * fu_security_attrs_append:
 * @self: A #FuSecurityAttrs
 * @attr: a #FwupdSecurityAttr
 *
 * Adds a #FwupdSecurityAttr to the array.
 *
 * Since: 1.5.0
 **/
void
fu_security_attrs_append (FuSecurityAttrs *self, FwupdSecurityAttr *attr)
{
	g_return_if_fail (FU_IS_SECURITY_ATTRS (self));
	g_return_if_fail (FWUPD_IS_SECURITY_ATTR (attr));

	/* sanity check */
	if (fwupd_security_attr_get_plugin (attr) == NULL) {
		g_warning ("%s has no plugin set",
			   fwupd_security_attr_get_appstream_id (attr));
	}
	g_ptr_array_add (self->attrs, g_object_ref (attr));
}

/**
 * fu_security_attrs_to_variant:
 * @self: A #FuSecurityAttrs
 *
 * Converts the #FwupdSecurityAttr objects into a variant array.
 *
 * Returns: a #GVariant or %NULL
 *
 * Since: 1.5.0
 **/
GVariant *
fu_security_attrs_to_variant (FuSecurityAttrs *self)
{
	GVariantBuilder builder;

	g_return_val_if_fail (FU_IS_SECURITY_ATTRS (self), NULL);
	g_return_val_if_fail (self->attrs->len > 0, NULL);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *security_attr = g_ptr_array_index (self->attrs, i);
		GVariant *tmp = fwupd_security_attr_to_variant (security_attr);
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(aa{sv})", &builder);
}

/**
 * fu_security_attrs_get_all:
 * @self: A #FuSecurityAttrs
 *
 * Gets all the attributes in the object.
 *
 * Returns: (transfer container) (element-type FwupdSecurityAttr): attributes
 *
 * Since: 1.5.0
 **/
GPtrArray *
fu_security_attrs_get_all (FuSecurityAttrs *self)
{
	g_return_val_if_fail (FU_IS_SECURITY_ATTRS (self), NULL);
	return g_ptr_array_ref (self->attrs);
}

/**
 * fu_security_attrs_calculate_hsi:
 * @self: A #FuSecurityAttrs
 *
 * Calculates the HSI string from the appended attribues.
 *
 * Returns: (transfer full): a string or %NULL
 *
 * Since: 1.5.0
 **/
gchar *
fu_security_attrs_calculate_hsi (FuSecurityAttrs *self)
{
	guint hsi_number = 0;
	FwupdSecurityAttrFlags flags = FWUPD_SECURITY_ATTR_FLAG_NONE;
	GString *str = g_string_new ("HSI:");
	const FwupdSecurityAttrFlags hpi_suffixes[] = {
		FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES,
		FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION,
		FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE,
		FWUPD_SECURITY_ATTR_FLAG_NONE,
	};

	g_return_val_if_fail (FU_IS_SECURITY_ATTRS (self), NULL);

	/* find the highest HSI number where there are no failures and at least
	 * one success */
	for (guint j = 1; j <= FWUPD_SECURITY_ATTR_LEVEL_LAST; j++) {
		gboolean success_cnt = 0;
		gboolean failure_cnt = 0;
		for (guint i = 0; i < self->attrs->len; i++) {
			FwupdSecurityAttr *attr = g_ptr_array_index (self->attrs, i);
			if (fwupd_security_attr_get_level (attr) != j)
				continue;
			if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				success_cnt++;
			else if (!fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED))
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
		FwupdSecurityAttr *attr = g_ptr_array_index (self->attrs, i);
		if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED))
			continue;
		/* positive things */
		if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES) ||
		    fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION)) {
			if (!fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				continue;
		}
		/* negative things */
		if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)) {
			if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				continue;
		}
		flags |= fwupd_security_attr_get_flags (attr);
	}

	g_string_append_printf (str, "%u", hsi_number);
	if (flags & (FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES |
		     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION |
		     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE)) {
		g_string_append (str, "+");
		for (guint j = 0; hpi_suffixes[j] != FWUPD_SECURITY_ATTR_FLAG_NONE; j++) {
			if (flags & hpi_suffixes[j])
				g_string_append (str, fwupd_security_attr_flag_to_suffix (hpi_suffixes[j]));
		}
	}
	return g_string_free (str, FALSE);
}

static gchar *
fu_security_attrs_get_sort_key (FwupdSecurityAttr *attr)
{
	GString *str = g_string_new (NULL);

	/* level */
	g_string_append_printf (str, "%u", fwupd_security_attr_get_level (attr));

	/* success -> fail -> obsoletes */
	if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_string_append (str, "0");
	} else if (!fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS) &&
		   !fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		g_string_append (str, "1");
	} else {
		g_string_append (str, "9");
	}

	/* prefer name, but fallback to appstream-id for tests */
	if (fwupd_security_attr_get_name (attr) != NULL) {
		g_string_append (str, fwupd_security_attr_get_name (attr));
	} else {
		g_string_append (str, fwupd_security_attr_get_appstream_id (attr));
	}
	return g_string_free (str, FALSE);
}

static gint
fu_security_attrs_sort_cb (gconstpointer item1, gconstpointer item2)
{
	FwupdSecurityAttr *attr1 = *((FwupdSecurityAttr **) item1);
	FwupdSecurityAttr *attr2 = *((FwupdSecurityAttr **) item2);
	g_autofree gchar *sort1 = fu_security_attrs_get_sort_key (attr1);
	g_autofree gchar *sort2 = fu_security_attrs_get_sort_key (attr2);
	return g_strcmp0 (sort1, sort2);
}

/**
 * fu_security_attrs_depsolve:
 * @self: A #FuSecurityAttrs
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
fu_security_attrs_depsolve (FuSecurityAttrs *self)
{
	g_autoptr(GHashTable) attrs_by_id = NULL;

	g_return_if_fail (FU_IS_SECURITY_ATTRS (self));

	/* make hash of ID -> object */
	attrs_by_id = g_hash_table_new (g_str_hash, g_str_equal);
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index (self->attrs, i);
		g_hash_table_insert (attrs_by_id,
				     (gpointer) fwupd_security_attr_get_appstream_id (attr),
				     (gpointer) attr);
	}

	/* set flat where required */
	for (guint i = 0; i < self->attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index (self->attrs, i);
		GPtrArray *obsoletes = fwupd_security_attr_get_obsoletes (attr);
		for (guint j = 0; j < obsoletes->len; j++) {
			const gchar *obsolete = g_ptr_array_index (obsoletes, j);
			FwupdSecurityAttr *attr_tmp = g_hash_table_lookup (attrs_by_id, obsolete);
			if (attr_tmp != NULL) {
				g_debug ("security attr %s obsoleted by %s", obsolete,
					 fwupd_security_attr_get_appstream_id (attr));
				fwupd_security_attr_add_flag (attr_tmp,
							      FWUPD_SECURITY_ATTR_FLAG_OBSOLETED);
			}
		}
	}

	/* sort */
	g_ptr_array_sort (self->attrs, fu_security_attrs_sort_cb);
}

/**
 * fu_security_attrs_new:
 *
 * Returns: a #FuSecurityAttrs
 *
 * Since: 1.5.0
 **/
FuSecurityAttrs *
fu_security_attrs_new (void)
{
	return g_object_new (FU_TYPE_SECURITY_ATTRS, NULL);
}
