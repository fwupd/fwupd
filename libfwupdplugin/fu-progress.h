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

FuProgress *
fu_progress_new(void);
const gchar *
fu_progress_get_id(FuProgress *self);
void
fu_progress_set_id(FuProgress *self, const gchar *id);
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
void
fu_progress_set_profile(FuProgress *self, gboolean profile);
void
fu_progress_reset(FuProgress *self);
void
fu_progress_set_steps(FuProgress *self, guint step_max);
void
fu_progress_add_step(FuProgress *self, FwupdStatus status, guint value);
void
fu_progress_finished(FuProgress *self);
void
fu_progress_step_done(FuProgress *self);
FuProgress *
fu_progress_get_child(FuProgress *self);
void
fu_progress_sleep(FuProgress *self, guint delay_ms);
