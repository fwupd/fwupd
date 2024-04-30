/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

/**
 * FwupdReportFlags:
 *
 * Flags used to represent report attributes
 */
typedef enum {
	/**
	 * FWUPD_REPORT_FLAG_NONE:
	 *
	 * No report flags are set.
	 *
	 * Since: 1.9.1
	 */
	FWUPD_REPORT_FLAG_NONE = 0u,
	/**
	 * FWUPD_REPORT_FLAG_FROM_OEM:
	 *
	 * The report was generated by the OEM.
	 *
	 * Since: 1.9.1
	 */
	FWUPD_REPORT_FLAG_FROM_OEM = 1ull << 0,
	/**
	 * FWUPD_REPORT_FLAG_IS_UPGRADE:
	 *
	 * The new firmware was newer than the old firmware.
	 *
	 * Since: 1.9.14
	 */
	FWUPD_REPORT_FLAG_IS_UPGRADE = 1ull << 1,
	/**
	 * FWUPD_REPORT_FLAG_UNKNOWN:
	 *
	 * The report flag is unknown.
	 *
	 * This is usually caused by a mismatched libfwupdplugin and daemon.
	 *
	 * Since: 1.9.1
	 */
	FWUPD_REPORT_FLAG_UNKNOWN = G_MAXUINT64,
} FwupdReportFlags;

FwupdReport *
fwupd_report_new(void);

guint64
fwupd_report_get_created(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_created(FwupdReport *self, guint64 created) G_GNUC_NON_NULL(1);
const gchar *
fwupd_report_get_version_old(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_version_old(FwupdReport *self, const gchar *version_old) G_GNUC_NON_NULL(1);
const gchar *
fwupd_report_get_vendor(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_vendor(FwupdReport *self, const gchar *vendor) G_GNUC_NON_NULL(1);
guint32
fwupd_report_get_vendor_id(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_vendor_id(FwupdReport *self, guint32 vendor_id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_report_get_device_name(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_device_name(FwupdReport *self, const gchar *device_name) G_GNUC_NON_NULL(1);
const gchar *
fwupd_report_get_distro_id(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_distro_id(FwupdReport *self, const gchar *distro_id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_report_get_distro_version(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_distro_version(FwupdReport *self, const gchar *distro_version) G_GNUC_NON_NULL(1);
const gchar *
fwupd_report_get_distro_variant(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_distro_variant(FwupdReport *self, const gchar *distro_variant) G_GNUC_NON_NULL(1);
const gchar *
fwupd_report_get_remote_id(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_remote_id(FwupdReport *self, const gchar *remote_id) G_GNUC_NON_NULL(1);

GHashTable *
fwupd_report_get_metadata(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_add_metadata_item(FwupdReport *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
const gchar *
fwupd_report_get_metadata_item(FwupdReport *self, const gchar *key) G_GNUC_NON_NULL(1, 2);

guint64
fwupd_report_get_flags(FwupdReport *self) G_GNUC_NON_NULL(1);
void
fwupd_report_set_flags(FwupdReport *self, guint64 flags) G_GNUC_NON_NULL(1);
void
fwupd_report_add_flag(FwupdReport *self, FwupdReportFlags flag) G_GNUC_NON_NULL(1);
void
fwupd_report_remove_flag(FwupdReport *self, FwupdReportFlags flag) G_GNUC_NON_NULL(1);
gboolean
fwupd_report_has_flag(FwupdReport *self, FwupdReportFlags flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

const gchar *
fwupd_report_flag_to_string(FwupdReportFlags report_flag);
FwupdReportFlags
fwupd_report_flag_from_string(const gchar *report_flag);

G_END_DECLS
