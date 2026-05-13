/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FwupdSecurityAttr"

#include "config.h"

#include "fu-security-attr.h"
#include "fu-version-common.h"

typedef struct {
	FuContext *ctx;
} FuSecurityAttrPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuSecurityAttr, fu_security_attr, FWUPD_TYPE_SECURITY_ATTR)

#define GET_PRIVATE(o) (fu_security_attr_get_instance_private(o))

/**
 * fu_security_attr_check_fwupd_version:
 * @attr: a #FwupdSecurityAttr
 * @fwupd_version: a fwupd version, e.g. `2.0.7`
 *
 * Checks if this attribute was available in a given fwupd release.
 *
 * If @fwupd_version is %NULL then expect %TRUE.
 *
 * Returns: %TRUE if the fwupd release contained this attribute
 *
 * Since: 2.0.7
 **/
gboolean
fu_security_attr_check_fwupd_version(FwupdSecurityAttr *attr, const gchar *fwupd_version)
{
	g_return_val_if_fail(FWUPD_IS_SECURITY_ATTR(attr), FALSE);
	if (fwupd_version == NULL)
		return TRUE;
	if (fwupd_security_attr_get_fwupd_version(attr) == NULL)
		return TRUE;
	return fu_version_compare(fwupd_version,
				  fwupd_security_attr_get_fwupd_version(attr),
				  FWUPD_VERSION_FORMAT_UNKNOWN) >= 0;
}

/**
 * fu_security_attr_add_bios_target_value:
 * @attr: a #FwupdSecurityAttr
 * @id: a #FwupdBiosSetting ID or name
 * @needle: The substring of a target value
 *
 * Checks all configured possible values of an enumeration attribute and
 * if any match @needle then set as the target value.
 *
 * Since: 1.8.4
 **/
void
fu_security_attr_add_bios_target_value(FwupdSecurityAttr *attr,
				       const gchar *id,
				       const gchar *needle)
{
	FuSecurityAttr *self = FU_SECURITY_ATTR(attr);
	FuSecurityAttrPrivate *priv = GET_PRIVATE(self);
	FwupdBiosSetting *bios_setting;
	GPtrArray *values;
	const gchar *current;

	bios_setting = fu_context_get_bios_setting(priv->ctx, id);
	if (bios_setting == NULL)
		return;
	current = fwupd_bios_setting_get_current_value(bios_setting);
	fwupd_security_attr_set_bios_setting_id(attr, fwupd_bios_setting_get_id(bios_setting));
	fwupd_security_attr_set_bios_setting_current_value(attr, current);
	if (fwupd_bios_setting_get_kind(bios_setting) != FWUPD_BIOS_SETTING_KIND_ENUMERATION)
		return;
	if (fwupd_bios_setting_get_read_only(bios_setting))
		return;
	values = fwupd_bios_setting_get_possible_values(bios_setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		g_autofree gchar *lower = g_utf8_strdown(possible, -1);
		if (g_strrstr(lower, needle)) {
			fwupd_security_attr_set_bios_setting_target_value(attr, possible);
			/* this is built-in to the engine */
			if (g_strcmp0(possible, current) != 0) {
				fwupd_security_attr_add_flag(attr,
							     FWUPD_SECURITY_ATTR_FLAG_CAN_FIX);
				fwupd_security_attr_add_flag(attr,
							     FWUPD_SECURITY_ATTR_FLAG_CAN_UNDO);
			}
			return;
		}
	}
}

static void
fu_security_attr_init(FuSecurityAttr *self)
{
}

static void
fu_security_attr_dispose(GObject *object)
{
	FuSecurityAttr *self = FU_SECURITY_ATTR(object);
	FuSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_clear_object(&priv->ctx);
	G_OBJECT_CLASS(fu_security_attr_parent_class)->dispose(object);
}

static void
fu_security_attr_class_init(FuSecurityAttrClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = fu_security_attr_dispose;
}

/**
 * fu_security_attr_new:
 * @ctx: a #FuContext
 * @appstream_id: (nullable): the AppStream component ID, e.g. `com.intel.BiosGuard`
 *
 * Creates a new #FwupdSecurityAttr with context set.
 *
 * Returns: (transfer full): a #FwupdSecurityAttr
 *
 * Since: 1.8.4
 **/
FwupdSecurityAttr *
fu_security_attr_new(FuContext *ctx, const gchar *appstream_id)
{
	g_autoptr(FuSecurityAttr) self = g_object_new(FU_TYPE_SECURITY_ATTR, NULL);
	FuSecurityAttrPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CONTEXT(ctx), NULL);
	if (appstream_id != NULL)
		fwupd_security_attr_set_appstream_id(FWUPD_SECURITY_ATTR(self), appstream_id);
	priv->ctx = g_object_ref(ctx);
	return FWUPD_SECURITY_ATTR(g_steal_pointer(&self));
}
