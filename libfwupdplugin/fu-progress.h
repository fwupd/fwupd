/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
fu_progress_get_id(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_set_id(FuProgress *self, const gchar *id) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_progress_get_name(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_set_name(FuProgress *self, const gchar *name) G_GNUC_NON_NULL(1, 2);
void
fu_progress_add_flag(FuProgress *self, FuProgressFlag flag) G_GNUC_NON_NULL(1);
void
fu_progress_remove_flag(FuProgress *self, FuProgressFlag flag) G_GNUC_NON_NULL(1);
gboolean
fu_progress_has_flag(FuProgress *self, FuProgressFlag flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FwupdStatus
fu_progress_get_status(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_set_status(FuProgress *self, FwupdStatus status) G_GNUC_NON_NULL(1);
void
fu_progress_set_percentage(FuProgress *self, guint percentage) G_GNUC_NON_NULL(1);
void
fu_progress_set_percentage_full(FuProgress *self, gsize progress_done, gsize progress_total)
    G_GNUC_NON_NULL(1);
guint
fu_progress_get_percentage(FuProgress *self) G_GNUC_NON_NULL(1);
gdouble
fu_progress_get_duration(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_set_profile(FuProgress *self, gboolean profile) G_GNUC_NON_NULL(1);
gboolean
fu_progress_get_profile(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_reset(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_set_steps(FuProgress *self, guint step_max) G_GNUC_NON_NULL(1);
guint
fu_progress_get_steps(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_add_step(FuProgress *self, FwupdStatus status, guint value, const gchar *name)
    G_GNUC_NON_NULL(1);
void
fu_progress_finished(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_step_done(FuProgress *self) G_GNUC_NON_NULL(1);
FuProgress *
fu_progress_get_child(FuProgress *self) G_GNUC_NON_NULL(1);
void
fu_progress_sleep(FuProgress *self, guint delay_ms) G_GNUC_NON_NULL(1);
gchar *
fu_progress_traceback(FuProgress *self) G_GNUC_NON_NULL(1);
