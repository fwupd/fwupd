/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine-config.h"
#include "fu-engine-request.h"

#define FU_TYPE_RELEASE (fu_release_get_type())
G_DECLARE_FINAL_TYPE(FuRelease, fu_release, FU, RELEASE, FwupdRelease)

FuRelease *
fu_release_new(void);

#define fu_release_get_appstream_id(r) fwupd_release_get_appstream_id(FWUPD_RELEASE(r))
#define fu_release_get_version(r)     fwupd_release_get_version(FWUPD_RELEASE(r))
#define fu_release_get_branch(r)      fwupd_release_get_branch(FWUPD_RELEASE(r))
#define fu_release_get_remote_id(r)    fwupd_release_get_remote_id(FWUPD_RELEASE(r))
#define fu_release_get_checksums(r)   fwupd_release_get_checksums(FWUPD_RELEASE(r))
#define fu_release_get_reports(r)      fwupd_release_get_reports(FWUPD_RELEASE(r))
#define fu_release_get_flags(r)	      fwupd_release_get_flags(FWUPD_RELEASE(r))
#define fu_release_add_flag(r, v)     fwupd_release_add_flag(FWUPD_RELEASE(r), v)
#define fu_release_has_flag(r, v)     fwupd_release_has_flag(FWUPD_RELEASE(r), v)
#define fu_release_add_tag(r, v)      fwupd_release_add_tag(FWUPD_RELEASE(r), v)
#define fu_release_add_metadata(r, v) fwupd_release_add_metadata(FWUPD_RELEASE(r), v)
#define fu_release_set_branch(r, v)   fwupd_release_set_branch(FWUPD_RELEASE(r), v)
#define fu_release_set_flags(r, v)    fwupd_release_set_flags(FWUPD_RELEASE(r), v)

gchar *
fu_release_to_string(FuRelease *self);
FuDevice *
fu_release_get_device(FuRelease *self);
GBytes *
fu_release_get_fw_blob(FuRelease *self);
FuEngineRequest *
fu_release_get_request(FuRelease *self);
GPtrArray *
fu_release_get_soft_reqs(FuRelease *self);
GPtrArray *
fu_release_get_hard_reqs(FuRelease *self);
const gchar *
fu_release_get_update_request_id(FuRelease *self);

void
fu_release_set_request(FuRelease *self, FuEngineRequest *request);
void
fu_release_set_device(FuRelease *self, FuDevice *device);
void
fu_release_set_remote(FuRelease *self, FwupdRemote *remote);
void
fu_release_set_config(FuRelease *self, FuEngineConfig *config);

gboolean
fu_release_load(FuRelease *self,
		XbNode *component,
		XbNode *rel,
		FwupdInstallFlags flags,
		GError **error);
const gchar *
fu_release_get_action_id(FuRelease *self);
gint
fu_release_compare(FuRelease *release1, FuRelease *release2);
void
fu_release_set_priority(FuRelease *self, guint64 priority);
