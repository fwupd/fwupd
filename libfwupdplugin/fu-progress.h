/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>
#include <gio/gio.h>

#define FU_TYPE_PROGRESS (fu_progress_get_type())
G_DECLARE_DERIVABLE_TYPE(FuProgress, fu_progress, FU, PROGRESS, GObject)

struct _FuProgressClass {
	GObjectClass parent_class;
	/* signals */
	void (*percentage_changed)(FuProgress *self, guint value);
	void (*status_changed)(FuProgress *self, FwupdStatus status);
	/*< private >*/
	gpointer padding[29];
};

/**
 * FuProgressFlags:
 *
 * The progress internal flags.
 **/
typedef guint64 FuProgressFlags;

/**
 * FU_PROGRESS_FLAG_NONE:
 *
 * No flags set.
 *
 * Since: 1.7.0
 */
#define FU_PROGRESS_FLAG_NONE (0)

/**
 * FU_PROGRESS_FLAG_UNKNOWN:
 *
 * Unknown flag value.
 *
 * Since: 1.7.0
 */
#define FU_PROGRESS_FLAG_UNKNOWN G_MAXUINT64

/**
 * FU_PROGRESS_FLAG_GUESSED:
 *
 * The steps have not been measured on real hardware and have been guessed.
 *
 * Since: 1.7.0
 */
#define FU_PROGRESS_FLAG_GUESSED (1ull << 0)

/**
 * FU_PROGRESS_FLAG_NO_PROFILE:
 *
 * The steps cannot be accurate enough for a profile result.
 *
 * Since: 1.7.0
 */
#define FU_PROGRESS_FLAG_NO_PROFILE (1ull << 1)

/**
 * FU_PROGRESS_FLAG_CHILD_FINISHED:
 *
 * The child completed all the expected steps.
 *
 * Since: 1.8.2
 */
#define FU_PROGRESS_FLAG_CHILD_FINISHED (1ull << 2)

/**
 * FU_PROGRESS_FLAG_NO_TRACEBACK:
 *
 * The steps should not be shown in the traceback.
 *
 * Since: 1.8.2
 */
#define FU_PROGRESS_FLAG_NO_TRACEBACK (1ull << 3)

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
const gchar *
fu_progress_flag_to_string(FuProgressFlags flag);
FuProgressFlags
fu_progress_flag_from_string(const gchar *flag);
void
fu_progress_add_flag(FuProgress *self, FuProgressFlags flag);
void
fu_progress_remove_flag(FuProgress *self, FuProgressFlags flag);
gboolean
fu_progress_has_flag(FuProgress *self, FuProgressFlags flag);
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
