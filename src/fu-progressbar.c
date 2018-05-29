/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "fu-progressbar.h"

static void fu_progressbar_finalize	 (GObject *obj);

struct _FuProgressbar
{
	GObject			 parent_instance;
	FwupdStatus		 status;
	gboolean		 spinner_count_up;	/* chars */
	guint			 spinner_idx;		/* chars */
	guint			 length_percentage;	/* chars */
	guint			 length_status;		/* chars */
	guint			 percentage;
	guint			 to_erase;		/* chars */
	guint			 timer_id;
	gint64			 last_animated;		/* monotonic */
};

G_DEFINE_TYPE (FuProgressbar, fu_progressbar, G_TYPE_OBJECT)

static const gchar *
fu_progressbar_status_to_string (FwupdStatus status)
{
	switch (status) {
	case FWUPD_STATUS_IDLE:
		/* TRANSLATORS: daemon is inactive */
		return _("Idle…");
		break;
	case FWUPD_STATUS_DECOMPRESSING:
		/* TRANSLATORS: decompressing the firmware file */
		return _("Decompressing…");
		break;
	case FWUPD_STATUS_LOADING:
		/* TRANSLATORS: parsing the firmware information */
		return _("Loading…");
		break;
	case FWUPD_STATUS_DEVICE_RESTART:
		/* TRANSLATORS: restarting the device to pick up new F/W */
		return _("Restarting device…");
		break;
	case FWUPD_STATUS_DEVICE_READ:
		/* TRANSLATORS: reading from the flash chips */
		return _("Reading…");
		break;
	case FWUPD_STATUS_DEVICE_WRITE:
		/* TRANSLATORS: writing to the flash chips */
		return _("Writing…");
		break;
	case FWUPD_STATUS_DEVICE_ERASE:
		/* TRANSLATORS: erasing contents of the flash chips */
		return _("Erasing…");
		break;
	case FWUPD_STATUS_DEVICE_VERIFY:
		/* TRANSLATORS: verifying we wrote the firmware correctly */
		return _("Verifying…");
		break;
	case FWUPD_STATUS_SCHEDULING:
		/* TRANSLATORS: scheduing an update to be done on the next boot */
		return _("Scheduling…");
		break;
	case FWUPD_STATUS_DOWNLOADING:
		/* TRANSLATORS: downloading from a remote server */
		return _("Downloading…");
		break;
	case FWUPD_STATUS_WAITING_FOR_AUTH:
		/* TRANSLATORS: waiting for user to authenticate */
		return _("Authenticating…");
		break;
	case FWUPD_STATUS_DEVICE_BUSY:
		/* TRANSLATORS: waiting for device to do something */
		return _("Waiting…");
		break;
	default:
		break;
	}

	/* TRANSLATORS: currect daemon status is unknown */
	return _("Unknown");
}

static void
fu_progressbar_refresh (FuProgressbar *self, FwupdStatus status, guint percentage)
{
	const gchar *title;
	guint i;
	gboolean is_idle_newline = FALSE;
	g_autoptr(GString) str = g_string_new (NULL);

	/* erase previous line */
	for (i = 0; i < self->to_erase; i++)
		g_print ("\b");

	/* add status */
	if (status == FWUPD_STATUS_IDLE) {
		percentage = 100;
		status = self->status;
		is_idle_newline = TRUE;
	}
	title = fu_progressbar_status_to_string (status);
	g_string_append (str, title);
	for (i = str->len; i < self->length_status; i++)
		g_string_append_c (str, ' ');

	/* add progressbar */
	g_string_append (str, "[");
	if (percentage > 0) {
		for (i = 0; i < (self->length_percentage - 1) * percentage / 100; i++)
			g_string_append_c (str, '*');
		for (i = i + 1; i < self->length_percentage; i++)
			g_string_append_c (str, ' ');
	} else {
		const gchar chars[] = { '-', '\\', '|', '/', };
		for (i = 0; i < self->spinner_idx; i++)
			g_string_append_c (str, ' ');
		g_string_append_c (str, chars[i / 4 % G_N_ELEMENTS(chars)]);
		for (i = i + 1; i < self->length_percentage - 1; i++)
			g_string_append_c (str, ' ');
	}
	g_string_append_c (str, ']');

	/* dump to screen */
	g_print ("%s", str->str);
	self->to_erase = str->len - 2;

	/* done */
	if (is_idle_newline) {
		g_print ("\n");
		self->to_erase = 0;
		return;
	}
}

static void
fu_progressbar_spin_inc (FuProgressbar *self)
{
	/* reset */
	self->last_animated = g_get_monotonic_time ();

	/* up to down */
	if (self->spinner_count_up) {
		if (++self->spinner_idx > self->length_percentage - 3)
			self->spinner_count_up = FALSE;
	} else {
		if (--self->spinner_idx == 0)
			self->spinner_count_up = TRUE;
	}
}

static gboolean
fu_progressbar_spin_cb (gpointer user_data)
{
	FuProgressbar *self = FU_PROGRESSBAR (user_data);

	/* ignore */
	if (self->status == FWUPD_STATUS_IDLE)
		return G_SOURCE_CONTINUE;

	/* move the spinner index up to down */
	fu_progressbar_spin_inc (self);

	/* update the terminal */
	fu_progressbar_refresh (self, self->status, self->percentage);

	return G_SOURCE_CONTINUE;
}

static void
fu_progressbar_spin_end (FuProgressbar *self)
{
	if (self->timer_id != 0) {
		g_source_remove (self->timer_id);
		self->timer_id = 0;
	}

	/* go back to the start when we next go into unknown percentage mode */
	self->spinner_idx = 0;
	self->spinner_count_up = TRUE;
}

static void
fu_progressbar_spin_start (FuProgressbar *self)
{
	if (self->timer_id != 0)
		g_source_remove (self->timer_id);
	self->timer_id = g_timeout_add (40, fu_progressbar_spin_cb, self);
}

void
fu_progressbar_update (FuProgressbar *self, FwupdStatus status, guint percentage)
{
	g_return_if_fail (FU_IS_PROGRESSBAR (self));

	/* use cached value */
	if (status == FWUPD_STATUS_UNKNOWN)
		status = self->status;

	/* if the main loop isn't spinning and we've not had a chance to
	 * execute the callback just do the refresh now manually */
	if (percentage == 0 &&
	    status != FWUPD_STATUS_IDLE &&
	    self->status != FWUPD_STATUS_UNKNOWN) {
		if ((g_get_monotonic_time () - self->last_animated) / 1000 > 40) {
			fu_progressbar_spin_inc (self);
			fu_progressbar_refresh (self, status, percentage);
		}
	}

	/* ignore duplicates */
	if (self->status == status &&
	    self->percentage == percentage)
		return;

	/* enable or disable the spinner timeout */
	if (percentage > 0) {
		fu_progressbar_spin_end (self);
	} else {
		fu_progressbar_spin_start (self);
	}

	/* update the terminal */
	fu_progressbar_refresh (self, status, percentage);

	/* cache */
	self->status = status;
	self->percentage = percentage;
}

void
fu_progressbar_set_length_status (FuProgressbar *self, guint len)
{
	g_return_if_fail (FU_IS_PROGRESSBAR (self));
	g_return_if_fail (len > 3);
	self->length_status = len;
}

void
fu_progressbar_set_length_percentage (FuProgressbar *self, guint len)
{
	g_return_if_fail (FU_IS_PROGRESSBAR (self));
	g_return_if_fail (len > 3);
	self->length_percentage = len;
}

static void
fu_progressbar_class_init (FuProgressbarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_progressbar_finalize;
}

static void
fu_progressbar_init (FuProgressbar *self)
{
	self->length_percentage = 40;
	self->length_status = 25;
	self->spinner_count_up = TRUE;
}

static void
fu_progressbar_finalize (GObject *obj)
{
	FuProgressbar *self = FU_PROGRESSBAR (obj);

	if (self->timer_id != 0)
		g_source_remove (self->timer_id);

	G_OBJECT_CLASS (fu_progressbar_parent_class)->finalize (obj);
}

FuProgressbar *
fu_progressbar_new (void)
{
	FuProgressbar *self;
	self = g_object_new (FU_TYPE_PROGRESSBAR, NULL);
	return FU_PROGRESSBAR (self);
}
