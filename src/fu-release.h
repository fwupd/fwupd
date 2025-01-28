/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cabinet.h"
#include "fu-engine-config.h"
#include "fu-engine-request.h"

#define FU_TYPE_RELEASE (fu_release_get_type())
G_DECLARE_FINAL_TYPE(FuRelease, fu_release, FU, RELEASE, FwupdRelease)

FuRelease *
fu_release_new(void);

#define fu_release_get_appstream_id(r)	 fwupd_release_get_appstream_id(FWUPD_RELEASE(r))
#define fu_release_get_filename(r)	 fwupd_release_get_filename(FWUPD_RELEASE(r))
#define fu_release_get_version(r)	 fwupd_release_get_version(FWUPD_RELEASE(r))
#define fu_release_get_branch(r)	 fwupd_release_get_branch(FWUPD_RELEASE(r))
#define fu_release_get_remote_id(r)	 fwupd_release_get_remote_id(FWUPD_RELEASE(r))
#define fu_release_get_checksums(r)	 fwupd_release_get_checksums(FWUPD_RELEASE(r))
#define fu_release_get_reports(r)	 fwupd_release_get_reports(FWUPD_RELEASE(r))
#define fu_release_get_flags(r)		 fwupd_release_get_flags(FWUPD_RELEASE(r))
#define fu_release_add_flag(r, v)	 fwupd_release_add_flag(FWUPD_RELEASE(r), v)
#define fu_release_has_flag(r, v)	 fwupd_release_has_flag(FWUPD_RELEASE(r), v)
#define fu_release_add_tag(r, v)	 fwupd_release_add_tag(FWUPD_RELEASE(r), v)
#define fu_release_add_metadata(r, v)	 fwupd_release_add_metadata(FWUPD_RELEASE(r), v)
#define fu_release_set_branch(r, v)	 fwupd_release_set_branch(FWUPD_RELEASE(r), v)
#define fu_release_set_description(r, v) fwupd_release_set_description(FWUPD_RELEASE(r), v)
#define fu_release_set_flags(r, v)	 fwupd_release_set_flags(FWUPD_RELEASE(r), v)
#define fu_release_set_filename(r, v)	 fwupd_release_set_filename(FWUPD_RELEASE(r), v)
#define fu_release_add_metadata_item(r, k, v)                                                      \
	fwupd_release_add_metadata_item(FWUPD_RELEASE(r), k, v)
#define fu_release_set_version(r, v)	   fwupd_release_set_version(FWUPD_RELEASE(r), v)
#define fu_release_set_protocol(r, v)	   fwupd_release_set_protocol(FWUPD_RELEASE(r), v)
#define fu_release_set_appstream_id(r, v)  fwupd_release_set_appstream_id(FWUPD_RELEASE(r), v)
#define fu_release_add_checksum(r, v)	   fwupd_release_add_checksum(FWUPD_RELEASE(r), v)
#define fu_release_set_id(r, v)		   fwupd_release_set_id(FWUPD_RELEASE(r), v)
#define fu_release_set_remote_id(r, v)	   fwupd_release_set_remote_id(FWUPD_RELEASE(r), v)
#define fu_release_set_filename(r, v)	   fwupd_release_set_filename(FWUPD_RELEASE(r), v)
#define fu_release_get_metadata_item(r, v) fwupd_release_get_metadata_item(FWUPD_RELEASE(r), v)
#define fu_release_get_protocol(r)	   fwupd_release_get_protocol(FWUPD_RELEASE(r))
#define fu_release_get_metadata(r)	   fwupd_release_get_metadata(FWUPD_RELEASE(r))
#define fu_release_get_id(r)		   fwupd_release_get_id(FWUPD_RELEASE(r))

gchar *
fu_release_to_string(FuRelease *self) G_GNUC_NON_NULL(1);
FuDevice *
fu_release_get_device(FuRelease *self) G_GNUC_NON_NULL(1);
GInputStream *
fu_release_get_stream(FuRelease *self) G_GNUC_NON_NULL(1);
FuEngineRequest *
fu_release_get_request(FuRelease *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_release_get_soft_reqs(FuRelease *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_release_get_hard_reqs(FuRelease *self) G_GNUC_NON_NULL(1);
const gchar *
fu_release_get_update_request_id(FuRelease *self) G_GNUC_NON_NULL(1);
const gchar *
fu_release_get_device_version_old(FuRelease *self) G_GNUC_NON_NULL(1);

void
fu_release_set_request(FuRelease *self, FuEngineRequest *request) G_GNUC_NON_NULL(1);
void
fu_release_set_device(FuRelease *self, FuDevice *device) G_GNUC_NON_NULL(1);
void
fu_release_set_remote(FuRelease *self, FwupdRemote *remote) G_GNUC_NON_NULL(1);
void
fu_release_set_config(FuRelease *self, FuEngineConfig *config) G_GNUC_NON_NULL(1);

gboolean
fu_release_load(FuRelease *self,
		FuCabinet *cabinet,
		XbNode *component,
		XbNode *rel,
		FwupdInstallFlags flags,
		GError **error) G_GNUC_NON_NULL(1, 3);
const gchar *
fu_release_get_action_id(FuRelease *self) G_GNUC_NON_NULL(1);
gint
fu_release_compare(FuRelease *release1, FuRelease *release2) G_GNUC_NON_NULL(1, 2);
void
fu_release_set_priority(FuRelease *self, guint64 priority) G_GNUC_NON_NULL(1);
guint64
fu_release_get_priority(FuRelease *self) G_GNUC_NON_NULL(1);
