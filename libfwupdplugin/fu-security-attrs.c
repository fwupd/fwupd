/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuSecurityAttrs"

#include "config.h"

#include <glib/gi18n.h>

#include "fwupd-security-attr-private.h"
#include "fwupd-version.h"

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
	GString *str = g_string_new("HSI:");
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
		if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA)) {
			g_string_append(str, "INVALID:missing-data");
			return g_string_free(str, FALSE);
		}

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

	return g_string_free(str, FALSE);
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
    {FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL},
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
