/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define FWUPD_TYPE_REPORT (fwupd_report_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdReport, fwupd_report, FWUPD, REPORT, GObject)

struct _FwupdReportClass {
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

FwupdReport *
fwupd_report_new(void);
gchar *
fwupd_report_to_string(FwupdReport *self);

guint64
fwupd_report_get_created(FwupdReport *self);
void
fwupd_report_set_created(FwupdReport *self, guint64 created);
const gchar *
fwupd_report_get_version_old(FwupdReport *self);
void
fwupd_report_set_version_old(FwupdReport *self, const gchar *version_old);
const gchar *
fwupd_report_get_vendor(FwupdReport *self);
void
fwupd_report_set_vendor(FwupdReport *self, const gchar *vendor);
guint32
fwupd_report_get_vendor_id(FwupdReport *self);
void
fwupd_report_set_vendor_id(FwupdReport *self, guint32 vendor_id);
const gchar *
fwupd_report_get_device_name(FwupdReport *self);
void
fwupd_report_set_device_name(FwupdReport *self, const gchar *device_name);
const gchar *
fwupd_report_get_distro_id(FwupdReport *self);
void
fwupd_report_set_distro_id(FwupdReport *self, const gchar *distro_id);
const gchar *
fwupd_report_get_distro_version(FwupdReport *self);
void
fwupd_report_set_distro_version(FwupdReport *self, const gchar *distro_version);
const gchar *
fwupd_report_get_distro_variant(FwupdReport *self);
void
fwupd_report_set_distro_variant(FwupdReport *self, const gchar *distro_variant);

GHashTable *
fwupd_report_get_metadata(FwupdReport *self);
void
fwupd_report_add_metadata_item(FwupdReport *self, const gchar *key, const gchar *value);
const gchar *
fwupd_report_get_metadata_item(FwupdReport *self, const gchar *key);

FwupdReport *
fwupd_report_from_variant(GVariant *value);
GVariant *
fwupd_report_to_variant(FwupdReport *self);

G_END_DECLS
