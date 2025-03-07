/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-enums.h"
#include "fwupd-report.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_RELEASE (fwupd_release_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdRelease, fwupd_release, FWUPD, RELEASE, GObject)

struct _FwupdReleaseClass {
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

FwupdRelease *
fwupd_release_new(void);

const gchar *
fwupd_release_get_version(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_version(FwupdRelease *self, const gchar *version) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_release_get_locations(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_add_location(FwupdRelease *self, const gchar *location) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_release_get_issues(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_add_issue(FwupdRelease *self, const gchar *issue) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_release_get_categories(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_add_category(FwupdRelease *self, const gchar *category) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_release_has_category(FwupdRelease *self, const gchar *category) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_release_get_checksums(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_add_checksum(FwupdRelease *self, const gchar *checksum) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_release_has_checksum(FwupdRelease *self, const gchar *checksum) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_release_get_tags(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_add_tag(FwupdRelease *self, const gchar *tag) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_release_has_tag(FwupdRelease *self, const gchar *tag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);

GHashTable *
fwupd_release_get_metadata(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_add_metadata(FwupdRelease *self, GHashTable *hash) G_GNUC_NON_NULL(1, 2);
void
fwupd_release_add_metadata_item(FwupdRelease *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
const gchar *
fwupd_release_get_metadata_item(FwupdRelease *self, const gchar *key) G_GNUC_NON_NULL(1, 2);

const gchar *
fwupd_release_get_filename(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_filename(FwupdRelease *self, const gchar *filename) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_protocol(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_protocol(FwupdRelease *self, const gchar *protocol) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_id(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_id(FwupdRelease *self, const gchar *id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_appstream_id(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_appstream_id(FwupdRelease *self, const gchar *appstream_id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_detach_caption(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_detach_caption(FwupdRelease *self, const gchar *detach_caption)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_detach_image(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_detach_image(FwupdRelease *self, const gchar *detach_image) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_remote_id(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_remote_id(FwupdRelease *self, const gchar *remote_id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_vendor(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_vendor(FwupdRelease *self, const gchar *vendor) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_name(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_name(FwupdRelease *self, const gchar *name) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_name_variant_suffix(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_name_variant_suffix(FwupdRelease *self, const gchar *name_variant_suffix)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_summary(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_summary(FwupdRelease *self, const gchar *summary) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_branch(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_branch(FwupdRelease *self, const gchar *branch) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_description(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_description(FwupdRelease *self, const gchar *description) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_homepage(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_homepage(FwupdRelease *self, const gchar *homepage) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_details_url(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_details_url(FwupdRelease *self, const gchar *details_url) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_source_url(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_source_url(FwupdRelease *self, const gchar *source_url) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_sbom_url(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_sbom_url(FwupdRelease *self, const gchar *sbom_url) G_GNUC_NON_NULL(1);
guint64
fwupd_release_get_size(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_size(FwupdRelease *self, guint64 size) G_GNUC_NON_NULL(1);
guint64
fwupd_release_get_created(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_created(FwupdRelease *self, guint64 created) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_license(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_license(FwupdRelease *self, const gchar *license) G_GNUC_NON_NULL(1);
FwupdReleaseFlags
fwupd_release_get_flags(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_flags(FwupdRelease *self, FwupdReleaseFlags flags) G_GNUC_NON_NULL(1);
void
fwupd_release_add_flag(FwupdRelease *self, FwupdReleaseFlags flag) G_GNUC_NON_NULL(1);
void
fwupd_release_remove_flag(FwupdRelease *self, FwupdReleaseFlags flag) G_GNUC_NON_NULL(1);
gboolean
fwupd_release_has_flag(FwupdRelease *self, FwupdReleaseFlags flag) G_GNUC_WARN_UNUSED_RESULT;
FwupdReleaseUrgency
fwupd_release_get_urgency(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_urgency(FwupdRelease *self, FwupdReleaseUrgency urgency) G_GNUC_NON_NULL(1);
guint32
fwupd_release_get_install_duration(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_install_duration(FwupdRelease *self, guint32 duration) G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_update_message(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_update_message(FwupdRelease *self, const gchar *update_message)
    G_GNUC_NON_NULL(1);
const gchar *
fwupd_release_get_update_image(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_set_update_image(FwupdRelease *self, const gchar *update_image) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_release_get_reports(FwupdRelease *self) G_GNUC_NON_NULL(1);
void
fwupd_release_add_report(FwupdRelease *self, FwupdReport *report) G_GNUC_NON_NULL(1);

gboolean
fwupd_release_match_flags(FwupdRelease *self, FwupdReleaseFlags include, FwupdReleaseFlags exclude)
    G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_release_array_filter_flags(GPtrArray *rels,
				 FwupdReleaseFlags include,
				 FwupdReleaseFlags exclude,
				 GError **error) G_GNUC_NON_NULL(1);

G_END_DECLS
