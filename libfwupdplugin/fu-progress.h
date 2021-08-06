/*
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_PROGRESS (fu_progress_get_type())
G_DECLARE_DERIVABLE_TYPE(FuProgress, fu_progress, FU, PROGRESS, GObject)

struct _FuProgressClass {
	GObjectClass parent_class;
	/* signals */
	void (*percentage_changed)(FuProgress *self, guint value);
	/*< private >*/
	gpointer padding[30];
};

#define fu_progress_set_steps(self, steps) fu_progress_set_steps_full(self, G_STRLOC, steps)
#define fu_progress_set_custom_steps(self, value, args...)                                         \
	fu_progress_set_custom_steps_full(self, G_STRLOC, value, ##args)

FuProgress *
fu_progress_new(void);
const gchar *
fu_progress_get_id(FuProgress *self);
gboolean
fu_progress_get_enabled(FuProgress *self);
void
fu_progress_set_enabled(FuProgress *self, gboolean enabled);
void
fu_progress_set_percentage(FuProgress *self, guint percentage);
void
fu_progress_set_percentage_full(FuProgress *self, gsize progress_done, gsize progress_total);
guint
fu_progress_get_percentage(FuProgress *self);
void
fu_progress_set_profile(FuProgress *self, gboolean profile);
void
fu_progress_reset(FuProgress *self);
void
fu_progress_set_steps_full(FuProgress *self, const gchar *id, guint step_max);
void
fu_progress_set_custom_steps_full(FuProgress *self, const gchar *id, guint value, ...);
void
fu_progress_finished(FuProgress *self);
void
fu_progress_step_done(FuProgress *self);
FuProgress *
fu_progress_get_division(FuProgress *self);
void
fu_progress_sleep(FuProgress *self, guint delay_secs);
