/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

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
gchar *
fwupd_security_attr_to_string(FwupdSecurityAttr *self);

const gchar *
fwupd_security_attr_get_appstream_id(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_appstream_id(FwupdSecurityAttr *self, const gchar *appstream_id);
FwupdSecurityAttrLevel
fwupd_security_attr_get_level(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_level(FwupdSecurityAttr *self, FwupdSecurityAttrLevel level);
FwupdSecurityAttrResult
fwupd_security_attr_get_result(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_result(FwupdSecurityAttr *self, FwupdSecurityAttrResult result);
const gchar *
fwupd_security_attr_get_name(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_name(FwupdSecurityAttr *self, const gchar *name);
const gchar *
fwupd_security_attr_get_plugin(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_plugin(FwupdSecurityAttr *self, const gchar *plugin);
const gchar *
fwupd_security_attr_get_url(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_url(FwupdSecurityAttr *self, const gchar *url);
guint64
fwupd_security_attr_get_created(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_created(FwupdSecurityAttr *self, guint64 created);
GPtrArray *
fwupd_security_attr_get_obsoletes(FwupdSecurityAttr *self);
void
fwupd_security_attr_add_obsolete(FwupdSecurityAttr *self, const gchar *appstream_id);
gboolean
fwupd_security_attr_has_obsolete(FwupdSecurityAttr *self, const gchar *appstream_id);
GPtrArray *
fwupd_security_attr_get_guids(FwupdSecurityAttr *self);
void
fwupd_security_attr_add_guid(FwupdSecurityAttr *self, const gchar *guid);
void
fwupd_security_attr_add_guids(FwupdSecurityAttr *self, GPtrArray *guids);
gboolean
fwupd_security_attr_has_guid(FwupdSecurityAttr *self, const gchar *guid);
const gchar *
fwupd_security_attr_get_metadata(FwupdSecurityAttr *self, const gchar *key);
void
fwupd_security_attr_add_metadata(FwupdSecurityAttr *self, const gchar *key, const gchar *value);
FwupdSecurityAttrFlags
fwupd_security_attr_get_flags(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_flags(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flags);
void
fwupd_security_attr_add_flag(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag);
gboolean
fwupd_security_attr_has_flag(FwupdSecurityAttr *self, FwupdSecurityAttrFlags flag);
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

FwupdSecurityAttr *
fwupd_security_attr_from_variant(GVariant *value);
GPtrArray *
fwupd_security_attr_array_from_variant(GVariant *value);

G_END_DECLS
