/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-enums.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_BIOS_ATTR (fwupd_bios_attr_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdBiosAttr, fwupd_bios_attr, FWUPD, BIOS_ATTR, GObject)

struct _FwupdBiosAttrClass {
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

/* special attributes */
#define FWUPD_BIOS_ATTR_PENDING_REBOOT "pending_reboot"
#define FWUPD_BIOS_ATTR_RESET_BIOS     "reset_bios"
#define FWUPD_BIOS_ATTR_DEBUG_CMD      "debug_cmd"

/**
 * FwupdBiosAttrKind:
 * @FWUPD_BIOS_ATTR_KIND_UNKNOWN:		BIOS attribute type is unknown
 * @FWUPD_BIOS_ATTR_KIND_ENUMERATION:		BIOS attribute that has enumerated possible values
 * @FWUPD_BIOS_ATTR_KIND_INTEGER:		BIOS attribute that is an integer
 * @FWUPD_BIOS_ATTR_KIND_STRING:		BIOS attribute that accepts a string
 *
 * The type of BIOS attribute.
 **/
typedef enum {
	FWUPD_BIOS_ATTR_KIND_UNKNOWN = 0,     /* Since: 1.8.4 */
	FWUPD_BIOS_ATTR_KIND_ENUMERATION = 1, /* Since: 1.8.4 */
	FWUPD_BIOS_ATTR_KIND_INTEGER = 2,     /* Since: 1.8.4 */
	FWUPD_BIOS_ATTR_KIND_STRING = 3,      /* Since: 1.8.4 */
	/*< private >*/
	FWUPD_BIOS_ATTR_KIND_LAST = 4 /* perhaps increased in the future */
} FwupdBiosAttrKind;

FwupdBiosAttr *
fwupd_bios_attr_new(const gchar *name, const gchar *path);
gchar *
fwupd_bios_attr_to_string(FwupdBiosAttr *self);

gboolean
fwupd_bios_attr_get_read_only(FwupdBiosAttr *self);
void
fwupd_bios_attr_set_read_only(FwupdBiosAttr *self, gboolean val);

guint64
fwupd_bios_attr_get_upper_bound(FwupdBiosAttr *self);
guint64
fwupd_bios_attr_get_lower_bound(FwupdBiosAttr *self);
guint64
fwupd_bios_attr_get_scalar_increment(FwupdBiosAttr *self);

void
fwupd_bios_attr_set_upper_bound(FwupdBiosAttr *self, guint64 val);
void
fwupd_bios_attr_set_lower_bound(FwupdBiosAttr *self, guint64 val);
void
fwupd_bios_attr_set_scalar_increment(FwupdBiosAttr *self, guint64 val);

void
fwupd_bios_attr_set_kind(FwupdBiosAttr *self, FwupdBiosAttrKind type);
void
fwupd_bios_attr_set_name(FwupdBiosAttr *self, const gchar *name);
void
fwupd_bios_attr_set_path(FwupdBiosAttr *self, const gchar *path);
void
fwupd_bios_attr_set_description(FwupdBiosAttr *self, const gchar *description);

FwupdBiosAttrKind
fwupd_bios_attr_get_kind(FwupdBiosAttr *self);
const gchar *
fwupd_bios_attr_get_name(FwupdBiosAttr *self);
const gchar *
fwupd_bios_attr_get_path(FwupdBiosAttr *self);
const gchar *
fwupd_bios_attr_get_description(FwupdBiosAttr *self);
gboolean
fwupd_bios_attr_has_possible_value(FwupdBiosAttr *self, const gchar *val);
void
fwupd_bios_attr_add_possible_value(FwupdBiosAttr *self, const gchar *possible_value);
GPtrArray *
fwupd_bios_attr_get_possible_values(FwupdBiosAttr *self);

FwupdBiosAttr *
fwupd_bios_attr_from_variant(GVariant *value);
GPtrArray *
fwupd_bios_attr_array_from_variant(GVariant *value);

const gchar *
fwupd_bios_attr_get_current_value(FwupdBiosAttr *self);
void
fwupd_bios_attr_set_current_value(FwupdBiosAttr *self, const gchar *value);

const gchar *
fwupd_bios_attr_get_id(FwupdBiosAttr *self);
void
fwupd_bios_attr_set_id(FwupdBiosAttr *self, const gchar *id);

G_END_DECLS
