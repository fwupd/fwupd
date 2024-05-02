/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "fwupd-build.h"
#include "fwupd-enums.h"

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

/**
 * FwupdSecurityAttrFlags:
 * @FWUPD_SECURITY_ATTR_FLAG_NONE:			No flags set
 * @FWUPD_SECURITY_ATTR_FLAG_SUCCESS:			Success
 * @FWUPD_SECURITY_ATTR_FLAG_OBSOLETED:			Obsoleted by another attribute
 * @FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA:		Missing data
 * @FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES:		Suffix `U`
 * @FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION:	Suffix `A`
 * @FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE:		Suffix `!`
 * @FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM:	Contact the firmware vendor for a update
 * @FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW:		Failure may be fixed by changing FW config
 * @FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS:		Failure may be fixed by changing OS config
 * @FWUPD_SECURITY_ATTR_FLAG_CAN_FIX:			The failure can be automatically fixed
 * @FWUPD_SECURITY_ATTR_FLAG_CAN_UNDO:			The fix can be automatically reverted
 *
 * The flags available for HSI attributes.
 **/
typedef enum {
	FWUPD_SECURITY_ATTR_FLAG_NONE = 0,
	FWUPD_SECURITY_ATTR_FLAG_SUCCESS = 1 << 0,
	FWUPD_SECURITY_ATTR_FLAG_OBSOLETED = 1 << 1,
	FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA = 1 << 2,
	FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES = 1 << 8,
	FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ATTESTATION = 1 << 9,
	FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE = 1 << 10,
	FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM = 1 << 11,
	FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW = 1 << 12,
	FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS = 1 << 13,
	FWUPD_SECURITY_ATTR_FLAG_CAN_FIX = 1 << 14,
	FWUPD_SECURITY_ATTR_FLAG_CAN_UNDO = 1 << 15,
} FwupdSecurityAttrFlags;

/**
 * FwupdSecurityAttrLevel:
 * @FWUPD_SECURITY_ATTR_LEVEL_NONE:			Very few detected firmware protections
 * @FWUPD_SECURITY_ATTR_LEVEL_CRITICAL:			The most basic of security protections
 * @FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT:		Firmware security issues considered
 *important
 * @FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL:		Firmware security issues that pose a
 *theoretical concern
 * @FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION:	Out-of-band protection of the system
 *firmware
 * @FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_ATTESTATION:	Out-of-band attestation of the system
 *firmware
 *
 * The HSI level.
 **/
typedef enum {
	FWUPD_SECURITY_ATTR_LEVEL_NONE = 0,		  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_LEVEL_CRITICAL = 1,		  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT = 2,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL = 3,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_PROTECTION = 4,  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_LEVEL_SYSTEM_ATTESTATION = 5, /* Since: 1.5.0 */
	/*< private >*/
	FWUPD_SECURITY_ATTR_LEVEL_LAST = 6 /* perhaps increased in the future */
} FwupdSecurityAttrLevel;

/**
 * FwupdSecurityAttrResult:
 * @FWUPD_SECURITY_ATTR_RESULT_UNKNOWN:			Not known
 * @FWUPD_SECURITY_ATTR_RESULT_ENABLED:			Enabled
 * @FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED:		Not enabled
 * @FWUPD_SECURITY_ATTR_RESULT_VALID:			Valid
 * @FWUPD_SECURITY_ATTR_RESULT_NOT_VALID:		Not valid
 * @FWUPD_SECURITY_ATTR_RESULT_LOCKED:			Locked
 * @FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED:		Not locked
 * @FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED:		Encrypted
 * @FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED:		Not encrypted
 * @FWUPD_SECURITY_ATTR_RESULT_TAINTED:			Tainted
 * @FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED:		Not tainted
 * @FWUPD_SECURITY_ATTR_RESULT_FOUND:			Found
 * @FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND:		NOt found
 * @FWUPD_SECURITY_ATTR_RESULT_SUPPORTED:		Supported
 * @FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED:		Not supported
 *
 * The HSI result.
 **/
typedef enum {
	FWUPD_SECURITY_ATTR_RESULT_UNKNOWN,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_ENABLED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_VALID,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_LOCKED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED, /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_TAINTED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_FOUND,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_SUPPORTED,	  /* Since: 1.5.0 */
	FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED, /* Since: 1.5.0 */
	/*< private >*/
	FWUPD_SECURITY_ATTR_RESULT_LAST
} FwupdSecurityAttrResult;

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
