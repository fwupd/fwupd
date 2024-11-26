/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-engine-struct.h"
#include "fu-engine.h"

#define FU_TYPE_ENGINE_EMULATOR (fu_engine_emulator_get_type())
G_DECLARE_FINAL_TYPE(FuEngineEmulator, fu_engine_emulator, FU, ENGINE_EMULATOR, GObject)

FuEngineEmulator *
fu_engine_emulator_new(FuEngine *engine) G_GNUC_NON_NULL(1);
gboolean
fu_engine_emulator_save(FuEngineEmulator *self, GOutputStream *stream, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_engine_emulator_load(FuEngineEmulator *self, GInputStream *stream, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_engine_emulator_load_phase(FuEngineEmulator *self, FuEngineEmulatorPhase phase, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_engine_emulator_save_phase(FuEngineEmulator *self, FuEngineEmulatorPhase phase, GError **error)
    G_GNUC_NON_NULL(1);
