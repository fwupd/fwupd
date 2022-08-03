/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FwupdSecurityAttr"

#include "config.h"

#include "fu-security-attr.h"

typedef struct {
	FuContext *ctx;
} FuSecurityAttrPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuSecurityAttr, fu_security_attr, FWUPD_TYPE_SECURITY_ATTR)

#define GET_PRIVATE(o) (fu_security_attr_get_instance_private(o))

/**
 * fu_security_attr_add_bios_target_value:
 * @attr: a #FwupdSecurityAttr
 * @id: a #FwupdBiosAttr ID or name
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
	FwupdBiosAttr *bios_attr;
	GPtrArray *values;

	bios_attr = fu_context_get_bios_attr(priv->ctx, id);
	if (bios_attr == NULL)
		return;
	fwupd_security_attr_set_bios_attr_id(attr, fwupd_bios_attr_get_id(bios_attr));
	fwupd_security_attr_set_bios_attr_current_value(
	    attr,
	    fwupd_bios_attr_get_current_value(bios_attr));
	if (fwupd_bios_attr_get_kind(bios_attr) != FWUPD_BIOS_ATTR_KIND_ENUMERATION)
		return;
	if (fwupd_bios_attr_get_read_only(bios_attr))
		return;
	values = fwupd_bios_attr_get_possible_values(bios_attr);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		g_autofree gchar *lower = g_utf8_strdown(possible, -1);
		if (g_strrstr(lower, needle)) {
			fwupd_security_attr_set_bios_attr_target_value(attr, possible);
			return;
		}
	}
}

static void
fu_security_attr_init(FuSecurityAttr *self)
{
}

static void
fu_security_attr_finalize(GObject *object)
{
	FuSecurityAttr *self = FU_SECURITY_ATTR(object);
	FuSecurityAttrPrivate *priv = GET_PRIVATE(self);
	if (priv->ctx != NULL)
		g_object_unref(priv->ctx);
	G_OBJECT_CLASS(fu_security_attr_parent_class)->finalize(object);
}

static void
fu_security_attr_class_init(FuSecurityAttrClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_security_attr_finalize;
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
