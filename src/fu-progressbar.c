/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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
};

G_DEFINE_TYPE (FuProgressbar, fu_progressbar, G_TYPE_OBJECT)

static const gchar *
fu_util_status_to_string (FwupdStatus status)
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
	case FWUPD_STATUS_DEVICE_WRITE:
		/* TRANSLATORS: writing to the flash chips */
		return _("Writing…");
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
	default:
		break;
	}

	/* TRANSLATORS: currect daemon status is unknown */
	return _("Unknown");
}

static void
fu_progressbar_refresh (FuProgressbar *self)
{
	const gchar *title;
	guint i;
	g_autoptr(GString) str = g_string_new (NULL);

	/* erase previous line */
	for (i = 0; i < self->to_erase; i++)
		g_print ("\b");

	/* add status */
	if (self->status == FWUPD_STATUS_IDLE) {
		if (self->to_erase > 0)
			g_print ("\n");
		self->to_erase = 0;
		return;
	}
	title = fu_util_status_to_string (self->status);
	g_string_append (str, title);
	for (i = str->len; i < self->length_status; i++)
		g_string_append (str, " ");

	/* add progressbar */
	g_string_append (str, "[");
	if (self->percentage > 0) {
		for (i = 0; i < self->length_percentage * self->percentage / 100; i++)
			g_string_append (str, "*");
		for (i = i + 1; i < self->length_percentage; i++)
			g_string_append (str, " ");
	} else {
		for (i = 0; i < self->spinner_idx; i++)
			g_string_append (str, " ");
		g_string_append (str, "*");
		for (i = i + 1; i < self->length_percentage; i++)
			g_string_append (str, " ");
	}
	g_string_append (str, "]");

	/* dump to screen */
	g_print ("%s", str->str);
	self->to_erase = str->len;
}

static gboolean
fu_progressbar_spin_cb (gpointer user_data)
{
	FuProgressbar *self = FU_PROGRESSBAR (user_data);

	/* move the spinner index up to down */
	if (self->spinner_count_up) {
		if (++self->spinner_idx > self->length_percentage - 2)
			self->spinner_count_up = FALSE;
	} else {
		if (--self->spinner_idx == 0)
			self->spinner_count_up = TRUE;
	}

	/* update the terminal */
	fu_progressbar_refresh (self);

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

	/* cache */
	self->status = status;
	self->percentage = percentage;

	/* enable or disable the spinner timeout */
	if (percentage > 0) {
		fu_progressbar_spin_end (self);
	} else {
		fu_progressbar_spin_start (self);
	}

	/* update the terminal */
	fu_progressbar_refresh (self);
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
