/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "fwupd-enums.h"
#include "fwupd-release.h"
#include "fwupd-request.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_DEVICE (fwupd_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdDevice, fwupd_device, FWUPD, DEVICE, GObject)

struct _FwupdDeviceClass {
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

FwupdDevice *
fwupd_device_new(void);

const gchar *
fwupd_device_get_id(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_id(FwupdDevice *self, const gchar *id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_parent_id(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_parent_id(FwupdDevice *self, const gchar *parent_id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_composite_id(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_composite_id(FwupdDevice *self, const gchar *composite_id) G_GNUC_NON_NULL(1);
FwupdDevice *
fwupd_device_get_root(FwupdDevice *self) G_GNUC_NON_NULL(1);
FwupdDevice *
fwupd_device_get_parent(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_parent(FwupdDevice *self, FwupdDevice *parent) G_GNUC_NON_NULL(1);
void
fwupd_device_add_child(FwupdDevice *self, FwupdDevice *child) G_GNUC_NON_NULL(1, 2);
void
fwupd_device_remove_child(FwupdDevice *self, FwupdDevice *child) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_device_get_children(FwupdDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_name(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_name(FwupdDevice *self, const gchar *name) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_serial(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_serial(FwupdDevice *self, const gchar *serial) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_summary(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_summary(FwupdDevice *self, const gchar *summary) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_branch(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_branch(FwupdDevice *self, const gchar *branch) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_version(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version(FwupdDevice *self, const gchar *version) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_version_lowest(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version_lowest(FwupdDevice *self, const gchar *version_lowest) G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_version_lowest_raw(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version_lowest_raw(FwupdDevice *self, guint64 version_lowest_raw)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_version_bootloader(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version_bootloader(FwupdDevice *self, const gchar *version_bootloader)
    G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_version_bootloader_raw(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version_bootloader_raw(FwupdDevice *self, guint64 version_bootloader_raw)
    G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_version_raw(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version_raw(FwupdDevice *self, guint64 version_raw) G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_version_build_date(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version_build_date(FwupdDevice *self, guint64 version_build_date)
    G_GNUC_NON_NULL(1);
FwupdVersionFormat
fwupd_device_get_version_format(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_version_format(FwupdDevice *self, FwupdVersionFormat version_format)
    G_GNUC_NON_NULL(1);
guint32
fwupd_device_get_flashes_left(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_flashes_left(FwupdDevice *self, guint32 flashes_left) G_GNUC_NON_NULL(1);
guint32
fwupd_device_get_battery_level(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_battery_level(FwupdDevice *self, guint32 battery_level) G_GNUC_NON_NULL(1);
guint32
fwupd_device_get_battery_threshold(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_battery_threshold(FwupdDevice *self, guint32 battery_threshold) G_GNUC_NON_NULL(1);
guint32
fwupd_device_get_install_duration(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_install_duration(FwupdDevice *self, guint32 duration) G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_flags(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_flags(FwupdDevice *self, guint64 flags) G_GNUC_NON_NULL(1);
void
fwupd_device_add_flag(FwupdDevice *self, FwupdDeviceFlags flag) G_GNUC_NON_NULL(1);
void
fwupd_device_remove_flag(FwupdDevice *self, FwupdDeviceFlags flag) G_GNUC_NON_NULL(1);
gboolean
fwupd_device_has_flag(FwupdDevice *self, FwupdDeviceFlags flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_problems(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_problems(FwupdDevice *self, guint64 problems) G_GNUC_NON_NULL(1);
void
fwupd_device_add_problem(FwupdDevice *self, FwupdDeviceProblem problem) G_GNUC_NON_NULL(1);
void
fwupd_device_remove_problem(FwupdDevice *self, FwupdDeviceProblem problem) G_GNUC_NON_NULL(1);
gboolean
fwupd_device_has_problem(FwupdDevice *self, FwupdDeviceProblem problem) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_request_flags(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_request_flags(FwupdDevice *self, guint64 request_flags) G_GNUC_NON_NULL(1);
void
fwupd_device_add_request_flag(FwupdDevice *self, FwupdRequestFlags request_flag) G_GNUC_NON_NULL(1);
void
fwupd_device_remove_request_flag(FwupdDevice *self, FwupdRequestFlags request_flag)
    G_GNUC_NON_NULL(1);
gboolean
fwupd_device_has_request_flag(FwupdDevice *self,
			      FwupdRequestFlags request_flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_created(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_created(FwupdDevice *self, guint64 created) G_GNUC_NON_NULL(1);
guint64
fwupd_device_get_modified(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_modified(FwupdDevice *self, guint64 modified) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_device_get_checksums(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_add_checksum(FwupdDevice *self, const gchar *checksum) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_has_checksum(FwupdDevice *self, const gchar *checksum) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
const gchar *
fwupd_device_get_plugin(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_plugin(FwupdDevice *self, const gchar *plugin) G_GNUC_NON_NULL(1);
void
fwupd_device_add_protocol(FwupdDevice *self, const gchar *protocol) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_has_protocol(FwupdDevice *self, const gchar *protocol) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_device_get_protocols(FwupdDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_vendor(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_vendor(FwupdDevice *self, const gchar *vendor) G_GNUC_NON_NULL(1);
void
fwupd_device_add_vendor_id(FwupdDevice *self, const gchar *vendor_id) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_has_vendor_id(FwupdDevice *self, const gchar *vendor_id) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_device_get_vendor_ids(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_add_guid(FwupdDevice *self, const gchar *guid) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_has_guid(FwupdDevice *self, const gchar *guid) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_device_get_guids(FwupdDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_guid_default(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_add_instance_id(FwupdDevice *self, const gchar *instance_id) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_has_instance_id(FwupdDevice *self, const gchar *instance_id) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_device_get_instance_ids(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_add_icon(FwupdDevice *self, const gchar *icon) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_has_icon(FwupdDevice *self, const gchar *icon) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_device_get_icons(FwupdDevice *self) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_device_get_issues(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_add_issue(FwupdDevice *self, const gchar *issue) G_GNUC_NON_NULL(1, 2);

FwupdUpdateState
fwupd_device_get_update_state(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_update_state(FwupdDevice *self, FwupdUpdateState update_state) G_GNUC_NON_NULL(1);
const gchar *
fwupd_device_get_update_error(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_update_error(FwupdDevice *self, const gchar *update_error) G_GNUC_NON_NULL(1);
FwupdStatus
fwupd_device_get_status(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_status(FwupdDevice *self, FwupdStatus status) G_GNUC_NON_NULL(1);
guint
fwupd_device_get_percentage(FwupdDevice *self) G_GNUC_NON_NULL(1);
void
fwupd_device_set_percentage(FwupdDevice *self, guint percentage) G_GNUC_NON_NULL(1);
void
fwupd_device_add_release(FwupdDevice *self, FwupdRelease *release) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_device_get_releases(FwupdDevice *self) G_GNUC_NON_NULL(1);
FwupdRelease *
fwupd_device_get_release_default(FwupdDevice *self) G_GNUC_NON_NULL(1);
gint
fwupd_device_compare(FwupdDevice *self1, FwupdDevice *self2) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_match_flags(FwupdDevice *self, FwupdDeviceFlags include, FwupdDeviceFlags exclude)
    G_GNUC_NON_NULL(1);

void
fwupd_device_array_ensure_parents(GPtrArray *devices) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_device_array_filter_flags(GPtrArray *devices,
				FwupdDeviceFlags include,
				FwupdDeviceFlags exclude,
				GError **error) G_GNUC_NON_NULL(1);

G_END_DECLS
