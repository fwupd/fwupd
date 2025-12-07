/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "fwupd-security-attr-struct.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_SECURITY_ATTR (fwupd_security_attr_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdSecurityAttr, fwupd_security_attr, FWUPD, SECURITY_ATTR, GObject)

struct _FwupdSecurityAttrClass {
	GObjectClass parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)(void);
	void (*_fwupd_reserved2)(void);
	void (*_fwupd_reserved3)(void);
	void (*_fwupd_reserved4)(void);
	void (*_fwupd_reserved5)(void);
	void (*_fwupd_reserved6)(void);
	void (*_fwupd_reserved7)(void);
};

FwupdSecurityAttr *
fwupd_security_attr_new(const gchar *appstream_id);

const gchar *
fwupd_security_attr_get_bios_setting_id(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_bios_setting_id(FwupdSecurityAttr *self, const gchar *id)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_bios_setting_target_value(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_bios_setting_target_value(FwupdSecurityAttr *self, const gchar *value)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_bios_setting_current_value(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_bios_setting_current_value(FwupdSecurityAttr *self, const gchar *value)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_kernel_current_value(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_kernel_current_value(FwupdSecurityAttr *self, const gchar *value)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_kernel_target_value(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_kernel_target_value(FwupdSecurityAttr *self, const gchar *value)
    G_GNUC_NON_NULL(1);

const gchar *
fwupd_security_attr_get_appstream_id(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_appstream_id(FwupdSecurityAttr *self, const gchar *appstream_id)
    G_GNUC_NON_NULL(1);
FwupdSecurityAttrLevel
fwupd_security_attr_get_level(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_level(FwupdSecurityAttr *self, FwupdSecurityAttrLevel level)
    G_GNUC_NON_NULL(1);
FwupdSecurityAttrResult
fwupd_security_attr_get_result(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_result(FwupdSecurityAttr *self, FwupdSecurityAttrResult result)
    G_GNUC_NON_NULL(1);
FwupdSecurityAttrResult
fwupd_security_attr_get_result_fallback(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_result_fallback(FwupdSecurityAttr *self, FwupdSecurityAttrResult result)
    G_GNUC_NON_NULL(1);
FwupdSecurityAttrResult
fwupd_security_attr_get_result_success(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_result_success(FwupdSecurityAttr *self, FwupdSecurityAttrResult result)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_name(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_name(FwupdSecurityAttr *self, const gchar *name) G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_title(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_title(FwupdSecurityAttr *self, const gchar *title) G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_description(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_description(FwupdSecurityAttr *self, const gchar *description)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_plugin(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_plugin(FwupdSecurityAttr *self, const gchar *plugin) G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_fwupd_version(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_fwupd_version(FwupdSecurityAttr *self, const gchar *fwupd_version)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_get_url(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_url(FwupdSecurityAttr *self, const gchar *url) G_GNUC_NON_NULL(1);
guint64
fwupd_security_attr_get_created(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_created(FwupdSecurityAttr *self, guint64 created) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_security_attr_get_obsoletes(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_add_obsolete(FwupdSecurityAttr *self, const gchar *appstream_id)
    G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_security_attr_has_obsolete(FwupdSecurityAttr *self,
				 const gchar *appstream_id) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_security_attr_get_guids(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_add_guid(FwupdSecurityAttr *self, const gchar *guid) G_GNUC_NON_NULL(1, 2);
void
fwupd_security_attr_add_guids(FwupdSecurityAttr *self, GPtrArray *guids) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_security_attr_has_guid(FwupdSecurityAttr *self, const gchar *guid) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
const gchar *
fwupd_security_attr_get_metadata(FwupdSecurityAttr *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
void
fwupd_security_attr_add_metadata(FwupdSecurityAttr *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
FwupdSecurityAttrFlags
fwupd_security_attr_get_flags(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);
void
fwupd_security_attr_set_flags(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flags)
    G_GNUC_NON_NULL(1);
void
fwupd_security_attr_add_flag(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag)
    G_GNUC_NON_NULL(1);
void
fwupd_security_attr_remove_flag(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag)
    G_GNUC_NON_NULL(1);
gboolean
fwupd_security_attr_has_flag(FwupdSecurityAttr *self,
			     FwupdSecurityAttrFlags flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_security_attr_flag_to_string(FwupdSecurityAttrFlags flag);
FwupdSecurityAttrFlags
fwupd_security_attr_flag_from_string(const gchar *flag);
const gchar *
fwupd_security_attr_flag_to_suffix(FwupdSecurityAttrFlags flag);
const gchar *
fwupd_security_attr_result_to_string(FwupdSecurityAttrResult result);
FwupdSecurityAttrResult
fwupd_security_attr_result_from_string(const gchar *result);

G_END_DECLS
