/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>
#include <gio/gio.h>

#include "fu-progress-struct.h"

#define FU_TYPE_PROGRESS (fu_progress_get_type())
G_DECLARE_FINAL_TYPE(FuProgress, fu_progress, FU, PROGRESS, GObject)

FuProgress *
fu_progress_new(const gchar *id);
const gchar *
fu_progress_get_id(FuProgress *self);
void
fu_progress_set_id(FuProgress *self, const gchar *id);
const gchar *
fu_progress_get_name(FuProgress *self);
void
fu_progress_set_name(FuProgress *self, const gchar *name);
void
fu_progress_add_flag(FuProgress *self, FuProgressFlag flag);
void
fu_progress_remove_flag(FuProgress *self, FuProgressFlag flag);
gboolean
fu_progress_has_flag(FuProgress *self, FuProgressFlag flag);
FwupdStatus
fu_progress_get_status(FuProgress *self);
void
fu_progress_set_status(FuProgress *self, FwupdStatus status);
void
fu_progress_set_percentage(FuProgress *self, guint percentage);
void
fu_progress_set_percentage_full(FuProgress *self, gsize progress_done, gsize progress_total);
guint
fu_progress_get_percentage(FuProgress *self);
gdouble
fu_progress_get_duration(FuProgress *self);
void
fu_progress_set_profile(FuProgress *self, gboolean profile);
gboolean
fu_progress_get_profile(FuProgress *self);
void
fu_progress_reset(FuProgress *self);
void
fu_progress_set_steps(FuProgress *self, guint step_max);
guint
fu_progress_get_steps(FuProgress *self);
void
fu_progress_add_step(FuProgress *self, FwupdStatus status, guint value, const gchar *name);
void
fu_progress_finished(FuProgress *self);
void
fu_progress_step_done(FuProgress *self);
FuProgress *
fu_progress_get_child(FuProgress *self);
void
fu_progress_sleep(FuProgress *self, guint delay_ms);
gchar *
fu_progress_traceback(FuProgress *self);
gchar *
fu_progress_to_string(FuProgress *self);
