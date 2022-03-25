/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>
#include <xmlb.h>

#include "fu-config.h"
#include "fu-device.h"
#include "fu-engine-request.h"

#define FU_TYPE_RELEASE (fu_release_get_type())
G_DECLARE_FINAL_TYPE(FuRelease, fu_release, FU, RELEASE, FwupdRelease)

FuRelease *
fu_release_new(void);

#define fu_release_get_version(r)     fwupd_release_get_version(FWUPD_RELEASE(r))
#define fu_release_get_branch(r)      fwupd_release_get_branch(FWUPD_RELEASE(r))
#define fu_release_get_checksums(r)   fwupd_release_get_checksums(FWUPD_RELEASE(r))
#define fu_release_add_flag(r, v)     fwupd_release_add_flag(FWUPD_RELEASE(r), v)
#define fu_release_add_tag(r, v)      fwupd_release_add_tag(FWUPD_RELEASE(r), v)
#define fu_release_add_metadata(r, v) fwupd_release_add_metadata(FWUPD_RELEASE(r), v)

FuDevice *
fu_release_get_device(FuRelease *self);
GBytes *
fu_release_get_fw_blob(FuRelease *self);
FuEngineRequest *
fu_release_get_request(FuRelease *self);
const gchar *
fu_release_get_builder_script(FuRelease *self);
const gchar *
fu_release_get_builder_output(FuRelease *self);
GPtrArray *
fu_release_get_soft_reqs(FuRelease *self);
GPtrArray *
fu_release_get_hard_reqs(FuRelease *self);

void
fu_release_set_request(FuRelease *self, FuEngineRequest *request);
void
fu_release_set_device(FuRelease *self, FuDevice *device);
void
fu_release_set_remote(FuRelease *self, FwupdRemote *remote);
void
fu_release_set_config(FuRelease *self, FuConfig *config);

gboolean
fu_release_load(FuRelease *self,
		XbNode *component,
		XbNode *rel,
		FwupdInstallFlags flags,
		GError **error);
FwupdReleaseFlags
fu_release_get_trust_flags(FuRelease *self);
const gchar *
fu_release_get_action_id(FuRelease *self);
gint
fu_release_compare(FuRelease *release1, FuRelease *release2);
