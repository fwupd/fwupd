/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-20 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "dfu-progress-bar.h"

typedef struct {
	guint			 position;
	gboolean		 move_forward;
} DfuProgressBarPulseState;

struct _DfuProgressBar {
	GObject			 parent_instance;
	guint			 size;
	gint			 percentage;
	guint			 padding;
	guint			 timer_id;
	DfuProgressBarPulseState pulse_state;
	gint			 tty_fd;
	gchar			*old_start_text;
};

#define DFU_PROGRESS_BAR_PERCENTAGE_INVALID	101
#define DFU_PROGRESS_BAR_PULSE_TIMEOUT		40 /* ms */

G_DEFINE_TYPE (DfuProgressBar, dfu_progress_bar, G_TYPE_OBJECT)

static void
dfu_progress_bar_console (DfuProgressBar *self, const gchar *tmp)
{
	gssize count;
	gssize wrote;
	count = strlen (tmp) + 1;
	wrote = write (self->tty_fd, tmp, count);
	if (wrote != count) {
		g_warning ("Only wrote %" G_GSSIZE_FORMAT
			   " of %" G_GSSIZE_FORMAT " bytes",
			   wrote, count);
	}
}

void
dfu_progress_bar_set_padding (DfuProgressBar *self, guint padding)
{
	g_return_if_fail (DFU_IS_PROGRESS_BAR (self));
	g_return_if_fail (padding < 100);
	self->padding = padding;
}

void
dfu_progress_bar_set_size (DfuProgressBar *self, guint size)
{
	g_return_if_fail (DFU_IS_PROGRESS_BAR (self));
	g_return_if_fail (size < 100);
	self->size = size;
}

static gboolean
dfu_progress_bar_draw (DfuProgressBar *self, gint percentage)
{
	guint section;
	guint i;
	GString *str;

	/* no value yet */
	if (percentage == G_MININT)
		return FALSE;

	/* restore cursor */
	str = g_string_new ("");
	g_string_append_printf (str, "%c8", 0x1B);

	section = (guint) ((gfloat) self->size / (gfloat) 100.0 * (gfloat) percentage);
	g_string_append (str, "[");
	for (i = 0; i < section; i++)
		g_string_append (str, "=");
	for (i = 0; i < self->size - section; i++)
		g_string_append (str, " ");
	g_string_append (str, "] ");
	if (percentage >= 0 && percentage < 100)
		g_string_append_printf (str, "(%i%%)  ", percentage);
	else
		g_string_append (str, "        ");
	dfu_progress_bar_console (self, str->str);
	g_string_free (str, TRUE);
	return TRUE;
}

static gboolean
dfu_progress_bar_pulse_bar (DfuProgressBar *self)
{
	gint i;
	g_autoptr(GString) str = NULL;

	/* restore cursor */
	str = g_string_new ("");
	g_string_append_printf (str, "%c8", 0x1B);

	if (self->pulse_state.move_forward) {
		if (self->pulse_state.position == self->size - 1)
			self->pulse_state.move_forward = FALSE;
		else
			self->pulse_state.position++;
	} else if (!self->pulse_state.move_forward) {
		if (self->pulse_state.position == 1)
			self->pulse_state.move_forward = TRUE;
		else
			self->pulse_state.position--;
	}

	g_string_append (str, "[");
	for (i = 0; i < (gint)self->pulse_state.position-1; i++)
		g_string_append (str, " ");
	g_string_append (str, "==");
	for (i = 0; i < (gint) (self->size - self->pulse_state.position - 1); i++)
		g_string_append (str, " ");
	g_string_append (str, "] ");
	if (self->percentage >= 0 && self->percentage != DFU_PROGRESS_BAR_PERCENTAGE_INVALID)
		g_string_append_printf (str, "(%i%%)  ", self->percentage);
	else
		g_string_append (str, "        ");
	dfu_progress_bar_console (self, str->str);

	return TRUE;
}

static void
dfu_progress_bar_draw_pulse_bar (DfuProgressBar *self)
{
	/* have we already got zero percent? */
	if (self->timer_id != 0)
		return;
	if (TRUE) {
		self->pulse_state.position = 1;
		self->pulse_state.move_forward = TRUE;
		self->timer_id = g_timeout_add (DFU_PROGRESS_BAR_PULSE_TIMEOUT,
						      (GSourceFunc) dfu_progress_bar_pulse_bar, self);
		g_source_set_name_by_id (self->timer_id, "[DfuProgressBar] pulse");
	}
}

void
dfu_progress_bar_set_percentage (DfuProgressBar *self, gint percentage)
{
	g_return_if_fail (DFU_IS_PROGRESS_BAR (self));
	g_return_if_fail (percentage <= DFU_PROGRESS_BAR_PERCENTAGE_INVALID);

	/* never called dfu_progress_bar_start() */
	if (self->percentage == G_MININT)
		dfu_progress_bar_start (self, "FIXME: need to call dfu_progress_bar_start() earlier!");

	/* check for old percentage */
	if (percentage == self->percentage) {
		g_debug ("skipping as the same");
		return;
	}

	/* save */
	self->percentage = percentage;

	/* either pulse or display */
	if (percentage < 0 || percentage > 100) {
		dfu_progress_bar_draw (self, 0);
		dfu_progress_bar_draw_pulse_bar (self);
	} else {
		if (self->timer_id != 0) {
			g_source_remove (self->timer_id);
			self->timer_id = 0;
		}
		dfu_progress_bar_draw (self, percentage);
	}
}

/**
 * pk_strpad:
 * @data: the input string
 * @length: the desired length of the output string, with padding
 *
 * Returns the text padded to a length with spaces. If the string is
 * longer than length then a longer string is returned.
 *
 * Return value: The padded string
 **/
static gchar *
pk_strpad (const gchar *data, guint length)
{
	gint size;
	guint data_len;
	gchar *text;
	gchar *padding;

	if (data == NULL)
		return g_strnfill (length, ' ');

	/* ITS4: ignore, only used for formatting */
	data_len = strlen (data);

	/* calculate */
	size = (length - data_len);
	if (size <= 0)
		return g_strdup (data);

	padding = g_strnfill (size, ' ');
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

void
dfu_progress_bar_start (DfuProgressBar *self, const gchar *text)
{
	g_autofree gchar *text_pad = NULL;
	g_autoptr(GString) str = NULL;

	g_return_if_fail (DFU_IS_PROGRESS_BAR (self));

	/* same as last time */
	if (g_strcmp0 (self->old_start_text, text) != 0) {
		g_free (self->old_start_text);
		self->old_start_text = g_strdup (text);
	}

	/* finish old value */
	str = g_string_new ("");
	if (self->percentage != G_MININT) {
		dfu_progress_bar_draw (self, 100);
		g_string_append (str, "\n");
	}

	/* make these all the same length */
	text_pad = pk_strpad (text, self->padding);
	g_string_append (str, text_pad);

	/* save cursor in new position */
	g_string_append_printf (str, "%c7", 0x1B);
	dfu_progress_bar_console (self, str->str);

	/* reset */
	if (self->percentage == G_MININT)
		self->percentage = 0;
	dfu_progress_bar_draw (self, 0);
}

void
dfu_progress_bar_end (DfuProgressBar *self)
{
	GString *str;

	g_return_if_fail (DFU_IS_PROGRESS_BAR (self));

	/* never drawn */
	if (self->percentage == G_MININT)
		return;

	self->percentage = G_MININT;
	dfu_progress_bar_draw (self, 100);
	str = g_string_new ("");
	g_string_append_printf (str, "\n");
	dfu_progress_bar_console (self, str->str);
	g_string_free (str, TRUE);
}

static void
dfu_progress_bar_finalize (GObject *object)
{
	DfuProgressBar *self;
	g_return_if_fail (DFU_IS_PROGRESS_BAR (object));
	self = DFU_PROGRESS_BAR (object);

	g_free (self->old_start_text);
	if (self->timer_id != 0)
		g_source_remove (self->timer_id);
	if (self->tty_fd > 0)
		close (self->tty_fd);
	G_OBJECT_CLASS (dfu_progress_bar_parent_class)->finalize (object);
}

static void
dfu_progress_bar_class_init (DfuProgressBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_progress_bar_finalize;
}

static void
dfu_progress_bar_init (DfuProgressBar *self)
{
	self->size = 10;
	self->percentage = G_MININT;
	self->padding = 0;
	self->timer_id = 0;
	self->tty_fd = open ("/dev/tty", O_RDWR, 0);
	if (self->tty_fd < 0)
		self->tty_fd = open ("/dev/console", O_RDWR, 0);
	if (self->tty_fd < 0)
		self->tty_fd = open ("/dev/stdout", O_RDWR, 0);
	g_assert (self->tty_fd > 0);
}

DfuProgressBar *
dfu_progress_bar_new (void)
{
	DfuProgressBar *self;
	self = g_object_new (DFU_TYPE_PROGRESS_BAR, NULL);
	return DFU_PROGRESS_BAR (self);
}
