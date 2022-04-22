/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuProgressBar"

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include "fu-common.h"
#include "fu-progressbar.h"

static void
fu_progressbar_finalize(GObject *obj);

struct _FuProgressbar {
	GObject parent_instance;
	GMainContext *main_ctx;
	FwupdStatus status;
	gboolean spinner_count_up; /* chars */
	guint spinner_idx;	   /* chars */
	guint length_percentage;   /* chars */
	guint length_status;	   /* chars */
	guint percentage;
	GSource *timer_source;
	gint64 last_animated; /* monotonic */
	GTimer *time_elapsed;
	gdouble last_estimate;
	gboolean interactive;
};

G_DEFINE_TYPE(FuProgressbar, fu_progressbar, G_TYPE_OBJECT)

static const gchar *
fu_progressbar_status_to_string(FwupdStatus status)
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
		/* TRANSLATORS: scheduling an update to be done on the next boot */
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

	/* TRANSLATORS: current daemon status is unknown */
	return _("Unknown");
}

static void
fu_progressbar_erase_line(FuProgressbar *self)
{
	if (!self->interactive)
		return;
	g_print("\033[G");
}

static gboolean
_fu_status_is_predictable(FwupdStatus status)
{
	if (status == FWUPD_STATUS_DEVICE_ERASE)
		return TRUE;
	if (status == FWUPD_STATUS_DEVICE_VERIFY)
		return TRUE;
	if (status == FWUPD_STATUS_DEVICE_READ)
		return TRUE;
	if (status == FWUPD_STATUS_DEVICE_WRITE)
		return TRUE;
	if (status == FWUPD_STATUS_DOWNLOADING)
		return TRUE;
	return FALSE;
}

static gboolean
fu_progressbar_estimate_ready(FuProgressbar *self, guint percentage)
{
	gdouble old;
	gdouble elapsed;

	/* now invalid */
	if (percentage == 0 || percentage == 100) {
		g_timer_start(self->time_elapsed);
		self->last_estimate = 0;
		return FALSE;
	}

	/* allow-list things that make sense... */
	if (!_fu_status_is_predictable(self->status))
		return FALSE;

	old = self->last_estimate;
	elapsed = g_timer_elapsed(self->time_elapsed, NULL);
	self->last_estimate = elapsed / percentage * (100 - percentage);

	/* estimate is ready if we have decreased */
	return old > self->last_estimate;
}

static gchar *
fu_progressbar_time_remaining_str(FuProgressbar *self)
{
	/* less than 5 seconds remaining */
	if (self->last_estimate < 5)
		return NULL;

	/* less than 60 seconds remaining */
	if (self->last_estimate < 60) {
		/* TRANSLATORS: time remaining for completing firmware flash */
		return g_strdup(_("Less than one minute remaining"));
	}

	return g_strdup_printf(
	    /* TRANSLATORS: more than a minute */
	    ngettext("%.0f minute remaining", "%.0f minutes remaining", self->last_estimate / 60),
	    self->last_estimate / 60);
}

static void
fu_progressbar_refresh(FuProgressbar *self, FwupdStatus status, guint percentage)
{
	const gchar *title;
	guint i;
	gboolean is_idle_newline = FALSE;
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_if_fail(percentage <= 100);

	/* erase previous line */
	fu_progressbar_erase_line(self);

	/* add status */
	if (status == FWUPD_STATUS_IDLE || status == FWUPD_STATUS_UNKNOWN) {
		status = self->status;
		is_idle_newline = TRUE;
	}
	if (percentage == 100)
		is_idle_newline = TRUE;
	title = fu_progressbar_status_to_string(status);
	g_string_append(str, title);
	for (i = fu_common_strwidth(str->str); i < self->length_status; i++)
		g_string_append_c(str, ' ');

	/* add progressbar */
	g_string_append(str, "[");
	if (percentage > 0) {
		for (i = 0; i < (self->length_percentage - 1) * percentage / 100; i++)
			g_string_append_c(str, '*');
		for (i = i + 1; i < self->length_percentage; i++)
			g_string_append_c(str, ' ');
	} else {
		const gchar chars[] = {
		    '-',
		    '\\',
		    '|',
		    '/',
		};
		for (i = 0; i < self->spinner_idx; i++)
			g_string_append_c(str, ' ');
		g_string_append_c(str, chars[i / 4 % G_N_ELEMENTS(chars)]);
		for (i = i + 1; i < self->length_percentage - 1; i++)
			g_string_append_c(str, ' ');
	}
	g_string_append_c(str, ']');

	/* once we have good data show an estimate of time remaining */
	if (fu_progressbar_estimate_ready(self, percentage)) {
		g_autofree gchar *remaining = fu_progressbar_time_remaining_str(self);
		if (remaining != NULL)
			g_string_append_printf(str, " %s…", remaining);
	}

	/* dump to screen */
	g_print("%s", str->str);

	/* done */
	if (is_idle_newline) {
		g_print("\n");
		return;
	}
}

/**
 * fu_progressbar_set_title:
 * @self: A #FuProgressbar
 * @title: A string
 *
 * Sets progressbar title
 *
 * Since: 0.9.7
 **/
void
fu_progressbar_set_title(FuProgressbar *self, const gchar *title)
{
	fu_progressbar_erase_line(self);
	g_print("%s\n", title);
	fu_progressbar_refresh(self, self->status, self->percentage);
}

/**
 * fu_progressbar_set_main_context:
 * @self: A #FuProgressbar
 * @main_ctx: (nullable): main context
 *
 * Sets progressbar main context to use for animations.
 *
 * Since: 1.6.2
 **/
void
fu_progressbar_set_main_context(FuProgressbar *self, GMainContext *main_ctx)
{
	self->main_ctx = g_main_context_ref(main_ctx);
}

static void
fu_progressbar_spin_inc(FuProgressbar *self)
{
	/* reset */
	self->last_animated = g_get_monotonic_time();

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
fu_progressbar_spin_cb(gpointer user_data)
{
	FuProgressbar *self = FU_PROGRESSBAR(user_data);

	/* ignore */
	if (self->status == FWUPD_STATUS_IDLE || self->status == FWUPD_STATUS_UNKNOWN)
		return G_SOURCE_CONTINUE;

	/* move the spinner index up to down */
	fu_progressbar_spin_inc(self);

	/* update the terminal */
	fu_progressbar_refresh(self, self->status, self->percentage);

	return G_SOURCE_CONTINUE;
}

static void
fu_progressbar_spin_end(FuProgressbar *self)
{
	if (self->timer_source != NULL) {
		g_source_destroy(self->timer_source);
		self->timer_source = NULL;

		/* reset when the spinner has been stopped */
		g_timer_start(self->time_elapsed);
	}

	/* go back to the start when we next go into unknown percentage mode */
	self->spinner_idx = 0;
	self->spinner_count_up = TRUE;
}

static void
fu_progressbar_spin_start(FuProgressbar *self)
{
	if (self->timer_source != NULL)
		g_source_destroy(self->timer_source);
	self->timer_source = g_timeout_source_new(40);
	g_source_set_callback(self->timer_source, fu_progressbar_spin_cb, self, NULL);
	g_source_attach(self->timer_source, self->main_ctx);
}

/**
 * fu_progressbar_update:
 * @self: A #FuProgressbar
 * @status: A #FwupdStatus
 * @percentage: unsigned integer
 *
 * Refreshes a progressbar
 *
 * Since: 0.9.7
 **/
void
fu_progressbar_update(FuProgressbar *self, FwupdStatus status, guint percentage)
{
	g_return_if_fail(FU_IS_PROGRESSBAR(self));

	/* not useful */
	if (status == FWUPD_STATUS_UNKNOWN)
		return;

	/* ignore initial client connection */
	if (self->status == FWUPD_STATUS_UNKNOWN && status == FWUPD_STATUS_IDLE) {
		self->status = status;
		return;
	}

	if (!self->interactive) {
		g_print("%s: %u%%\n", fu_progressbar_status_to_string(status), percentage);
		self->status = status;
		self->percentage = percentage;
		return;
	}

	/* if the main loop isn't spinning and we've not had a chance to
	 * execute the callback just do the refresh now manually */
	if (percentage == 0 && status != FWUPD_STATUS_IDLE &&
	    self->status != FWUPD_STATUS_UNKNOWN) {
		if ((g_get_monotonic_time() - self->last_animated) / 1000 > 40) {
			fu_progressbar_spin_inc(self);
			fu_progressbar_refresh(self, status, percentage);
		}
	}

	/* ignore duplicates */
	if (self->status == status && self->percentage == percentage)
		return;

	/* enable or disable the spinner timeout */
	if (percentage > 0) {
		fu_progressbar_spin_end(self);
	} else {
		fu_progressbar_spin_start(self);
	}

	/* update the terminal */
	fu_progressbar_refresh(self, status, percentage);

	/* cache */
	self->status = status;
	self->percentage = percentage;
}

/**
 * fu_progressbar_set_interactive:
 * @self: A #FuProgressbar
 * @interactive: #gboolean
 *
 * Marks the progressbar as interactive or not
 *
 * Since: 0.9.7
 **/
void
fu_progressbar_set_interactive(FuProgressbar *self, gboolean interactive)
{
	g_return_if_fail(FU_IS_PROGRESSBAR(self));
	self->interactive = interactive;
}

/**
 * fu_progressbar_set_length_status:
 * @self: A #FuProgressbar
 * @len: unsigned integer
 *
 * Sets the length of the progressbar status
 *
 * Since: 0.9.7
 **/
void
fu_progressbar_set_length_status(FuProgressbar *self, guint len)
{
	g_return_if_fail(FU_IS_PROGRESSBAR(self));
	g_return_if_fail(len > 3);
	self->length_status = len;
}

/**
 * fu_progressbar_set_length_percentage:
 * @self: A #FuProgressbar
 * @len: unsigned integer
 *
 * Sets the length of the progressba percentage
 *
 * Since: 0.9.7
 **/
void
fu_progressbar_set_length_percentage(FuProgressbar *self, guint len)
{
	g_return_if_fail(FU_IS_PROGRESSBAR(self));
	g_return_if_fail(len > 3);
	self->length_percentage = len;
}

static void
fu_progressbar_class_init(FuProgressbarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_progressbar_finalize;
}

static void
fu_progressbar_init(FuProgressbar *self)
{
	self->length_percentage = 40;
	self->length_status = 25;
	self->spinner_count_up = TRUE;
	self->time_elapsed = g_timer_new();
	self->interactive = TRUE;
}

static void
fu_progressbar_finalize(GObject *obj)
{
	FuProgressbar *self = FU_PROGRESSBAR(obj);

	if (self->timer_source != 0)
		g_source_destroy(self->timer_source);
	if (self->main_ctx != NULL)
		g_main_context_unref(self->main_ctx);
	g_timer_destroy(self->time_elapsed);

	G_OBJECT_CLASS(fu_progressbar_parent_class)->finalize(obj);
}

/**
 * fu_progressbar_new:
 *
 * Creates a new #FuProgressbar
 *
 * Since: 0.9.7
 **/
FuProgressbar *
fu_progressbar_new(void)
{
	FuProgressbar *self;
	self = g_object_new(FU_TYPE_PROGRESSBAR, NULL);
	return FU_PROGRESSBAR(self);
}
