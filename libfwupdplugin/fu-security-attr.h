/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libfwupd/fwupd-security-attr.h>

#include "fu-context.h"

G_BEGIN_DECLS

#define FU_TYPE_SECURITY_ATTR (fu_security_attr_get_type())
G_DECLARE_DERIVABLE_TYPE(FuSecurityAttr, fu_security_attr, FU, SECURITY_ATTR, FwupdSecurityAttr)

struct _FuSecurityAttrClass {
	FwupdSecurityAttrClass parent_class;
};

FuSecurityAttr *
fu_security_attr_new(FuContext *ctx, const gchar *appstream_id) G_GNUC_NON_NULL(1);
void
fu_security_attr_add_bios_target_value(FuSecurityAttr *self, const gchar *id, const gchar *needle)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_security_attr_check_fwupd_version(FuSecurityAttr *self, const gchar *fwupd_version)
    G_GNUC_NON_NULL(1) G_GNUC_PURE;
FuSecurityAttr *
fu_security_attr_copy(FuSecurityAttr *self) G_GNUC_NON_NULL(1);

#define fu_security_attr_add_flag(s, v)	 fwupd_security_attr_add_flag(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_add_guid(s, v)	 fwupd_security_attr_add_guid(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_add_guids(s, v) fwupd_security_attr_add_guids(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_add_metadata(s, k, v)                                                     \
	fwupd_security_attr_add_metadata(FWUPD_SECURITY_ATTR(s), k, v)
#define fu_security_attr_add_obsolete(s, v)                                                        \
	fwupd_security_attr_add_obsolete(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_get_appstream_id(s)                                                       \
	fwupd_security_attr_get_appstream_id(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_bios_setting_current_value(s)                                         \
	fwupd_security_attr_get_bios_setting_current_value(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_bios_setting_id(s)                                                    \
	fwupd_security_attr_get_bios_setting_id(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_bios_setting_target_value(s)                                          \
	fwupd_security_attr_get_bios_setting_target_value(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_created(s) fwupd_security_attr_get_created(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_description(s)                                                        \
	fwupd_security_attr_get_description(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_flags(s) fwupd_security_attr_get_flags(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_fwupd_version(s)                                                      \
	fwupd_security_attr_get_fwupd_version(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_kernel_current_value(s)                                               \
	fwupd_security_attr_get_kernel_current_value(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_kernel_target_value(s)                                                \
	fwupd_security_attr_get_kernel_target_value(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_level(s) fwupd_security_attr_get_level(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_metadata(s, v)                                                        \
	fwupd_security_attr_get_metadata(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_get_name(s)	  fwupd_security_attr_get_name(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_obsoletes(s) fwupd_security_attr_get_obsoletes(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_plugin(s)	  fwupd_security_attr_get_plugin(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_result_fallback(s)                                                    \
	fwupd_security_attr_get_result_fallback(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_result_success(s)                                                     \
	fwupd_security_attr_get_result_success(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_result(s)	fwupd_security_attr_get_result(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_title(s)	fwupd_security_attr_get_title(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_get_url(s)	fwupd_security_attr_get_url(FWUPD_SECURITY_ATTR(s))
#define fu_security_attr_has_flag(s, v) fwupd_security_attr_has_flag(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_has_obsolete(s, v)                                                        \
	fwupd_security_attr_has_obsolete(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_remove_flag(s, v)                                                         \
	fwupd_security_attr_remove_flag(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_appstream_id(s, v)                                                    \
	fwupd_security_attr_set_appstream_id(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_bios_setting_current_value(s, v)                                      \
	fwupd_security_attr_set_bios_setting_current_value(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_bios_setting_id(s, v)                                                 \
	fwupd_security_attr_set_bios_setting_id(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_bios_setting_target_value(s, v)                                       \
	fwupd_security_attr_set_bios_setting_target_value(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_created(s, v)                                                         \
	fwupd_security_attr_set_created(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_description(s, v)                                                     \
	fwupd_security_attr_set_description(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_flags(s, v) fwupd_security_attr_set_flags(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_fwupd_version(s, v)                                                   \
	fwupd_security_attr_set_fwupd_version(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_kernel_current_value(s, v)                                            \
	fwupd_security_attr_set_kernel_current_value(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_kernel_target_value(s, v)                                             \
	fwupd_security_attr_set_kernel_target_value(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_level(s, v)  fwupd_security_attr_set_level(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_name(s, v)	  fwupd_security_attr_set_name(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_plugin(s, v) fwupd_security_attr_set_plugin(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_result_fallback(s, v)                                                 \
	fwupd_security_attr_set_result_fallback(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_result_success(s, v)                                                  \
	fwupd_security_attr_set_result_success(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_result(s, v) fwupd_security_attr_set_result(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_title(s, v)  fwupd_security_attr_set_title(FWUPD_SECURITY_ATTR(s), v)
#define fu_security_attr_set_url(s, v)	  fwupd_security_attr_set_url(FWUPD_SECURITY_ATTR(s), v)

G_END_DECLS
