/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine.h"

G_BEGIN_DECLS

#define FU_TYPE_ENGINE_INSTALLER (fu_engine_installer_get_type())
G_DECLARE_FINAL_TYPE(FuEngineInstaller, fu_engine_installer, FU, ENGINE_INSTALLER, GObject)

FuEngineInstaller *
fu_engine_installer_new(FuEngine *engine) G_GNUC_NON_NULL(1);
gboolean
fu_engine_installer_build(FuEngineInstaller *self,
			  const gchar *device_id,
			  FuInputStream *stream,
			  FwupdInstallFlags install_flags,
			  GError **error) G_GNUC_NON_NULL(1, 2, 3);

/* in */
void
fu_engine_installer_set_request(FuEngineInstaller *self, FuEngineRequest *request)
    G_GNUC_NON_NULL(1);

/* out */
GPtrArray *
fu_engine_installer_get_releases(FuEngineInstaller *self) G_GNUC_NON_NULL(1);
gchar *
fu_engine_installer_pop_action_id(FuEngineInstaller *self) G_GNUC_NON_NULL(1);

G_END_DECLS
